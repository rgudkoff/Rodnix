#ifndef _RODNIX_NET_PROTO_H
#define _RODNIX_NET_PROTO_H

/*
 * Internal boundary between the socket layer and the protocol engine.
 *
 * The protocol engine (proto_ip.c, proto_tcp.c, proto_udp.c, proto_icmp.c)
 * owns wire formats, the TCP state machine, and link/route resolution. It has
 * no knowledge of net_socket_t, file descriptors, or blocking.
 *
 * The socket layer (socket.c) owns net_socket_t, the port tables, per-socket
 * queues, the accept queue, and every blocking loop.
 *
 * Downcalls (socket -> engine) are declared in the per-protocol headers.
 * Upcalls (engine -> socket) go exclusively through net_proto_sink_t, which
 * the socket layer registers at init. This keeps the engine independently
 * testable: supply a sink and drive it with frames, no sockets required.
 */

#include <stddef.h>
#include <stdint.h>

#include "socket.h"
#include "bsd_ether.h"
#include "../fabric/service/net_service.h"

struct tcp_conn;

/* -------------------------------------------------------------------------
 * Upcalls: protocol engine -> socket layer
 * ---------------------------------------------------------------------- */

typedef struct net_proto_sink {
    /* Deliver a validated inbound UDP frame to whichever socket owns dport.
     * Returns 0 if delivered, -1 if there is no listener. */
    int (*udp_deliver)(uint16_t dport, const void* frame, size_t frame_len);

    /* Report the accept backlog configured for a listening TCP port.
     * Returns 0 and fills out_backlog/out_queued, or -1 if no listener. */
    int (*tcp_listen_query)(uint16_t port, uint32_t* out_backlog,
                            uint32_t* out_queued);

    /* Hand a fully established inbound connection to the listening socket.
     * Ownership of `conn` transfers to the socket layer on success. */
    int (*tcp_accept_enqueue)(uint16_t port, struct tcp_conn* conn);
} net_proto_sink_t;

void net_proto_set_sink(const net_proto_sink_t* sink);
const net_proto_sink_t* net_proto_sink(void);

/* -------------------------------------------------------------------------
 * Link, route, and ARP helpers (proto_ip.c)
 * ---------------------------------------------------------------------- */

extern const uint8_t net_mac_loopback[BSD_ETHER_ADDR_LEN];

void net_link_init_once(void);
int  net_ensure_dispatch_path(void);

int  net_is_local_ipv4(uint32_t ip, fabric_netif_t** out_iface);
fabric_netif_t* net_select_tx_iface(uint32_t dst_host);

/* First non-loopback UP interface, preferring one whose address is dst_ip. */
fabric_netif_t* net_select_wire_iface(uint32_t dst_ip);

/* Next-hop address for dst_host on iface — gateway when off-link. */
uint32_t net_next_hop(fabric_netif_t* iface, uint32_t dst_host);

int  net_resolve_arp(fabric_netif_t* iface, uint32_t target_ip,
                     uint8_t mac_out[BSD_ETHER_ADDR_LEN]);

int  net_tx_params(fabric_netif_t* tx_iface, uint32_t dst_host,
                   uint32_t* src_host_out,
                   uint8_t src_mac_out[BSD_ETHER_ADDR_LEN],
                   uint8_t dst_mac_out[BSD_ETHER_ADDR_LEN]);

/*
 * Emit a fully built frame. Frames addressed to one of our own wire
 * addresses are looped back into the ingress path instead of being handed
 * to the NIC; everything else is transmitted normally.
 * Returns 0 on success, -1 otherwise.
 */
int  net_frame_emit(fabric_netif_t* iface, uint32_t dst_host,
                    const void* frame, uint32_t frame_len);

/* -------------------------------------------------------------------------
 * Time helpers shared by both layers
 * ---------------------------------------------------------------------- */

uint64_t net_ticks_to_ms(uint64_t ticks);
uint64_t net_deadline_from_ms(uint64_t timeout_ms);

#endif /* _RODNIX_NET_PROTO_H */
