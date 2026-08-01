#ifndef _RODNIX_PROTO_TCP_H
#define _RODNIX_PROTO_TCP_H

/*
 * TCP protocol engine.
 *
 * Owns the connection table, the state machine, and segment construction.
 * Knows nothing about sockets: inbound connections are handed up through
 * net_proto_sink_t, and every entry point here is non-blocking. Callers that
 * need to wait poll tcp_conn_state() from the socket layer.
 */

#include <stddef.h>
#include <stdint.h>

#include "bsd_ether.h"
#include "bsd_inet.h"
#include "../fabric/spin.h"
#include "../fabric/service/net_service.h"

#define TCP_ST_SYN_RCVD    1u
#define TCP_ST_ESTABLISHED 2u
#define TCP_ST_CLOSE_WAIT  3u
#define TCP_ST_SYN_SENT    4u   /* client: sent SYN, awaiting SYN-ACK */
#define TCP_ST_FIN_WAIT_1  5u   /* sent FIN, awaiting ACK */
#define TCP_ST_FIN_WAIT_2  6u   /* got ACK of our FIN, awaiting peer FIN */
#define TCP_ST_TIME_WAIT   7u   /* received peer FIN, sent final ACK */

#define TCP_RX_BUF_SZ      8192u
#define TCP_TX_BUF_SZ      1460u   /* one segment at a time */
#define TCP_DFLT_WIN       0xFFFFu
#define TCP_CONNECT_TIMEOUT_MS 5000u

typedef struct tcp_conn {
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t snd_nxt;           /* next seq num to send */
    uint32_t rcv_nxt;           /* next byte expected from peer */
    uint8_t  state;
    int      is_local;          /* 1 = kernel-local (no wire) */

    uint8_t  rx_buf[TCP_RX_BUF_SZ];
    uint32_t rx_len;            /* bytes available to read */
    spinlock_t lock;

    struct tcp_conn* peer;      /* non-NULL for local connections */
    struct tcp_conn* next;      /* intrusive link for accept queue */
} tcp_conn_t;

/* --- engine lifecycle ---------------------------------------------------- */

void tcp_engine_init(void);

/* --- connection table ---------------------------------------------------- */

tcp_conn_t* tcp_conn_find(uint16_t local_port, uint32_t remote_ip,
                          uint16_t remote_port);
void tcp_conn_insert(tcp_conn_t* conn);
void tcp_conn_remove(uint16_t local_port, uint32_t remote_ip,
                     uint16_t remote_port);
uint16_t tcp_alloc_ephemeral_port(void);

/* --- connection state access (socket layer polls these) ------------------ */

uint8_t tcp_conn_state(tcp_conn_t* conn);

/* Copy up to `len` received bytes out of the connection.
 * Returns byte count (>0), 0 on peer EOF, or -1 when nothing is available. */
int tcp_conn_rx_take(tcp_conn_t* conn, void* buf, size_t len);

/* --- outbound ------------------------------------------------------------ */

/* Build and transmit a single segment. data/data_len may be NULL/0. */
int tcp_send_segment(fabric_netif_t* iface,
                     uint32_t src_ip, uint16_t src_port,
                     uint32_t dst_ip, uint16_t dst_port,
                     const uint8_t dst_mac[BSD_ETHER_ADDR_LEN],
                     uint32_t seq, uint32_t ack,
                     uint8_t flags,
                     const void* data, size_t data_len);

/*
 * Start an outbound connection: allocate the connection, resolve the next
 * hop, emit SYN, and register it. Returns a connection in TCP_ST_SYN_SENT,
 * or NULL. The caller polls tcp_conn_state() for TCP_ST_ESTABLISHED.
 */
tcp_conn_t* tcp_engine_open(uint32_t dst_ip, uint16_t dst_port);

/* Allocate a kernel-local (loopback) connection pair, both ESTABLISHED. */
int tcp_engine_open_local_pair(uint16_t local_port, uint16_t remote_port,
                               uint32_t remote_ip,
                               tcp_conn_t** out_srv, tcp_conn_t** out_cli);

/* Segment and transmit `len` bytes. Returns bytes sent, or -1. */
int tcp_engine_send(tcp_conn_t* conn, const void* buf, size_t len);

/* Copy into a kernel-local peer's receive buffer. Returns bytes, or -1. */
int tcp_engine_send_local(tcp_conn_t* conn, const void* buf, size_t len);

/*
 * Emit FIN and move to TCP_ST_FIN_WAIT_1. Returns 0 if a FIN went out (the
 * caller may then poll for TCP_ST_TIME_WAIT), -1 if no FIN was sent.
 */
int tcp_engine_send_fin(tcp_conn_t* conn);

/* Unregister and free a connection. */
void tcp_engine_release(tcp_conn_t* conn);

/* --- inbound ------------------------------------------------------------- */

/* Process one inbound TCP segment. Returns 0 if handled, -1 to ignore. */
int tcp_input(const uint8_t* frame, uint32_t frame_len,
              const bsd_ether_header_t* eh, const bsd_ip_t* ip);

#endif /* _RODNIX_PROTO_TCP_H */
