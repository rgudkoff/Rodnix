/*
 * proto_tcp.c — TCP protocol engine.
 *
 * Owns the connection table, the state machine, and segment construction.
 * The socket layer is reachable only through net_proto_sink_t, so this file
 * can be driven by a test harness that supplies frames and a stub sink —
 * no socket table and no NIC required.
 *
 * Nothing here blocks. Entry points that logically wait (connect, close)
 * return once the protocol action is emitted; the socket layer polls
 * tcp_conn_state() for the transition it needs.
 */

#include "proto_tcp.h"
#include "net_proto.h"
#include "socket.h"
#include "bsd_inet.h"
#include "bsd_ether.h"
#include "stack.h"
#include "../kernel/fabric/service/net_service.h"
#include "../kernel/fabric/spin.h"
#include "../lib/heap.h"
#include "../sched/scheduler.h"
#include "../include/common.h"
#include "../include/error.h"

#define TCP_PORT_TABLE_SZ 65536

/* Half-open (SYN_RCVD/SYN_SENT) and established connections, bucketed by
 * local port and chained via ->next. Demux matches the full 4-tuple. */
static tcp_conn_t* g_tcp_conn_list[TCP_PORT_TABLE_SZ];
static spinlock_t  g_tcp_lock;

void tcp_engine_init(void)
{
    spinlock_init(&g_tcp_lock);
}

/* =========================================================================
 * Connection table
 * ======================================================================= */

tcp_conn_t* tcp_conn_find(uint16_t local_port,
                          uint32_t remote_ip, uint16_t remote_port)
{
    spinlock_lock(&g_tcp_lock);
    tcp_conn_t* c = g_tcp_conn_list[local_port];
    while (c) {
        if (c->remote_ip == remote_ip && c->remote_port == remote_port) {
            spinlock_unlock(&g_tcp_lock);
            return c;
        }
        c = c->next;
    }
    spinlock_unlock(&g_tcp_lock);
    return NULL;
}

static void tcp_conn_remove_locked(uint16_t local_port,
                                   uint32_t remote_ip, uint16_t remote_port)
{
    tcp_conn_t** pp = &g_tcp_conn_list[local_port];
    while (*pp) {
        tcp_conn_t* c = *pp;
        if (c->remote_ip == remote_ip && c->remote_port == remote_port) {
            *pp = c->next;
            return;
        }
        pp = &c->next;
    }
}

void tcp_conn_insert(tcp_conn_t* conn)
{
    spinlock_lock(&g_tcp_lock);
    conn->next = g_tcp_conn_list[conn->local_port];
    g_tcp_conn_list[conn->local_port] = conn;
    spinlock_unlock(&g_tcp_lock);
}

void tcp_conn_remove(uint16_t local_port,
                     uint32_t remote_ip, uint16_t remote_port)
{
    spinlock_lock(&g_tcp_lock);
    tcp_conn_remove_locked(local_port, remote_ip, remote_port);
    spinlock_unlock(&g_tcp_lock);
}

/* True when `port` is claimed by a listening socket. The listen table lives
 * in the socket layer, so this goes out through the sink — and deliberately
 * not while g_tcp_lock is held, to keep the two locks independent. */
static int tcp_port_has_listener(uint16_t port)
{
    const net_proto_sink_t* sink = net_proto_sink();
    uint32_t backlog = 0;
    uint32_t queued = 0;

    if (!sink || !sink->tcp_listen_query) {
        return 0;
    }
    return (sink->tcp_listen_query(port, &backlog, &queued) == 0) ? 1 : 0;
}

uint16_t tcp_alloc_ephemeral_port(void)
{
    static uint16_t g_eph_next = 49152u;

    for (uint32_t i = 0; i < (65535u - 49152u); i++) {
        spinlock_lock(&g_tcp_lock);
        uint16_t p = g_eph_next++;
        if (g_eph_next == 0) {
            g_eph_next = 49152u;
        }
        int conn_free = (g_tcp_conn_list[p] == NULL);
        spinlock_unlock(&g_tcp_lock);

        if (conn_free && !tcp_port_has_listener(p)) {
            return p;
        }
    }
    return 0;
}

static uint32_t tcp_gen_isn(uint32_t src_ip, uint16_t src_port,
                            uint32_t dst_ip, uint16_t dst_port)
{
    uint64_t t = scheduler_get_ticks();
    return (uint32_t)(t * 6364136223846793005ULL
                      ^ ((uint64_t)src_ip << 16 | src_port)
                      ^ ((uint64_t)dst_ip << 16 | dst_port));
}

/* =========================================================================
 * Connection state access
 * ======================================================================= */

uint8_t tcp_conn_state(tcp_conn_t* conn)
{
    if (!conn) {
        return 0;
    }
    spinlock_lock(&conn->lock);
    uint8_t st = conn->state;
    spinlock_unlock(&conn->lock);
    return st;
}

int tcp_conn_rx_take(tcp_conn_t* conn, void* buf, size_t len)
{
    if (!conn || !buf || len == 0) {
        return -1;
    }

    spinlock_lock(&conn->lock);
    if (conn->rx_len > 0) {
        size_t to_copy = (conn->rx_len < len) ? conn->rx_len : len;
        memcpy(buf, conn->rx_buf, to_copy);
        if (to_copy < conn->rx_len) {
            memmove(conn->rx_buf, conn->rx_buf + to_copy,
                    conn->rx_len - to_copy);
        }
        conn->rx_len -= (uint32_t)to_copy;
        spinlock_unlock(&conn->lock);
        return (int)to_copy;
    }
    if (conn->state == TCP_ST_CLOSE_WAIT) {
        spinlock_unlock(&conn->lock);
        return 0;  /* EOF */
    }
    spinlock_unlock(&conn->lock);
    return -1;
}

/* =========================================================================
 * Segment transmission
 * ======================================================================= */

int tcp_send_segment(fabric_netif_t* iface,
                     uint32_t src_ip, uint16_t src_port,
                     uint32_t dst_ip, uint16_t dst_port,
                     const uint8_t dst_mac[BSD_ETHER_ADDR_LEN],
                     uint32_t seq, uint32_t ack,
                     uint8_t flags,
                     const void* data, size_t data_len)
{
    const size_t eth_len = sizeof(bsd_ether_header_t);
    const size_t ip_len  = sizeof(bsd_ip_t);
    const size_t tcp_len = sizeof(bsd_tcphdr_t);
    const size_t frame_len = eth_len + ip_len + tcp_len + data_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return -1;
    }
    memset(frame, 0, frame_len);

    /* Ethernet header */
    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_dhost, dst_mac, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, iface->mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    /* IP header */
    bsd_ip_t* ip = (bsd_ip_t*)(frame + eth_len);
    ip->ip_vhl  = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos  = 0;
    ip->ip_len  = bsd_htons((uint16_t)(ip_len + tcp_len + data_len));
    ip->ip_id   = 0;
    ip->ip_off  = bsd_htons(BSD_IP_DF);
    ip->ip_ttl  = BSD_IP_TTL_DEF;
    ip->ip_p    = BSD_IPPROTO_TCP;
    ip->ip_sum  = 0;
    ip->ip_src  = bsd_htonl(src_ip);
    ip->ip_dst  = bsd_htonl(dst_ip);
    ip->ip_sum  = bsd_htons(bsd_in_cksum(ip, ip_len));

    /* TCP header */
    bsd_tcphdr_t* th = (bsd_tcphdr_t*)(frame + eth_len + ip_len);
    th->th_sport = bsd_htons(src_port);
    th->th_dport = bsd_htons(dst_port);
    th->th_seq   = bsd_htonl(seq);
    th->th_ack   = bsd_htonl(ack);
    th->th_off   = (uint8_t)(sizeof(bsd_tcphdr_t) / 4) << 4;
    th->th_flags = flags;
    th->th_win   = bsd_htons(TCP_DFLT_WIN);
    th->th_sum   = 0;
    th->th_urp   = 0;

    if (data && data_len > 0) {
        memcpy(frame + eth_len + ip_len + tcp_len, data, data_len);
    }

    th->th_sum = bsd_htons(bsd_tcp4_checksum(src_ip, dst_ip, th, data, data_len));

    int rc = (fabric_netif_tx(iface, frame, (uint32_t)frame_len) == RDNX_OK) ? 0 : -1;
    kfree(frame);
    return rc;
}

/* =========================================================================
 * Outbound connection setup
 * ======================================================================= */

tcp_conn_t* tcp_engine_open(uint32_t dst_ip, uint16_t dst_port)
{
    fabric_netif_t* iface = net_select_tx_iface(dst_ip);
    if (!iface) {
        return NULL;
    }

    uint16_t cli_port = tcp_alloc_ephemeral_port();
    if (cli_port == 0) {
        return NULL;
    }

    tcp_conn_t* c = (tcp_conn_t*)kmalloc(sizeof(tcp_conn_t));
    if (!c) {
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->remote_ip   = dst_ip;
    c->remote_port = dst_port;
    c->local_port  = cli_port;
    c->snd_nxt     = tcp_gen_isn(iface->ipv4_addr, cli_port, dst_ip, dst_port);
    c->rcv_nxt     = 0;
    c->state       = TCP_ST_SYN_SENT;
    c->is_local    = 0;
    spinlock_init(&c->lock);
    tcp_conn_insert(c);

    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    if (net_resolve_arp(iface, net_next_hop(iface, dst_ip), dst_mac) != 0) {
        tcp_conn_remove(cli_port, dst_ip, dst_port);
        kfree(c);
        return NULL;
    }

    (void)tcp_send_segment(iface, iface->ipv4_addr, cli_port,
                           dst_ip, dst_port, dst_mac,
                           c->snd_nxt, 0, BSD_TH_SYN, NULL, 0);
    c->snd_nxt++;

    return c;
}

int tcp_engine_open_local_pair(uint16_t local_port, uint16_t remote_port,
                               uint32_t remote_ip,
                               tcp_conn_t** out_srv, tcp_conn_t** out_cli)
{
    if (!out_srv || !out_cli) {
        return -1;
    }

    tcp_conn_t* srv_conn = (tcp_conn_t*)kmalloc(sizeof(tcp_conn_t));
    tcp_conn_t* cli_conn = (tcp_conn_t*)kmalloc(sizeof(tcp_conn_t));
    if (!srv_conn || !cli_conn) {
        kfree(srv_conn);
        kfree(cli_conn);
        return -1;
    }
    memset(srv_conn, 0, sizeof(*srv_conn));
    memset(cli_conn, 0, sizeof(*cli_conn));

    srv_conn->local_port  = local_port;
    srv_conn->remote_port = remote_port;
    srv_conn->remote_ip   = NET_LOOPBACK_ADDR;
    srv_conn->state       = TCP_ST_ESTABLISHED;
    srv_conn->is_local    = 1;
    srv_conn->peer        = cli_conn;
    spinlock_init(&srv_conn->lock);

    cli_conn->local_port  = remote_port;
    cli_conn->remote_port = local_port;
    cli_conn->remote_ip   = remote_ip;
    cli_conn->state       = TCP_ST_ESTABLISHED;
    cli_conn->is_local    = 1;
    cli_conn->peer        = srv_conn;
    spinlock_init(&cli_conn->lock);

    *out_srv = srv_conn;
    *out_cli = cli_conn;
    return 0;
}

/* =========================================================================
 * Data transmission
 * ======================================================================= */

int tcp_engine_send_local(tcp_conn_t* conn, const void* buf, size_t len)
{
    if (!conn || !buf || len == 0) {
        return -1;
    }
    tcp_conn_t* peer = conn->peer;
    if (!peer) {
        return -1;
    }

    spinlock_lock(&peer->lock);
    size_t space = TCP_RX_BUF_SZ - peer->rx_len;
    size_t to_copy = (len < space) ? len : space;
    if (to_copy > 0) {
        memcpy(peer->rx_buf + peer->rx_len, buf, to_copy);
        peer->rx_len += (uint32_t)to_copy;
    }
    spinlock_unlock(&peer->lock);
    return (to_copy > 0) ? (int)to_copy : -1;
}

int tcp_engine_send(tcp_conn_t* conn, const void* buf, size_t len)
{
    if (!conn || !buf || len == 0) {
        return -1;
    }

    if (conn->is_local) {
        return tcp_engine_send_local(conn, buf, len);
    }

    fabric_netif_t* iface = net_select_wire_iface(conn->remote_ip);
    if (!iface) {
        return -1;
    }

    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    if (bsd_arp_lookup(net_next_hop(iface, conn->remote_ip), dst_mac) != 0) {
        return -1;
    }

    const uint8_t* p = (const uint8_t*)buf;
    size_t remaining = len;
    int sent = 0;

    while (remaining > 0) {
        size_t chunk = (remaining < TCP_TX_BUF_SZ) ? remaining : TCP_TX_BUF_SZ;

        int rc = tcp_send_segment(iface,
                                  iface->ipv4_addr, conn->local_port,
                                  conn->remote_ip,  conn->remote_port,
                                  dst_mac,
                                  conn->snd_nxt, conn->rcv_nxt,
                                  BSD_TH_ACK | BSD_TH_PSH,
                                  p, chunk);
        if (rc != 0) {
            break;
        }
        conn->snd_nxt += (uint32_t)chunk;
        p         += chunk;
        remaining -= chunk;
        sent      += (int)chunk;
    }

    return (sent > 0) ? sent : -1;
}

int tcp_engine_send_fin(tcp_conn_t* conn)
{
    if (!conn) {
        return -1;
    }

    spinlock_lock(&conn->lock);
    uint8_t st = conn->state;

    if (conn->is_local || (st != TCP_ST_ESTABLISHED && st != TCP_ST_CLOSE_WAIT)) {
        spinlock_unlock(&conn->lock);
        return -1;
    }

    fabric_netif_t* iface = net_select_tx_iface(conn->remote_ip);
    if (!iface) {
        spinlock_unlock(&conn->lock);
        return -1;
    }

    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    if (bsd_arp_lookup(net_next_hop(iface, conn->remote_ip), dst_mac) != 0) {
        spinlock_unlock(&conn->lock);
        return -1;
    }

    uint32_t snd = conn->snd_nxt;
    uint32_t rcv = conn->rcv_nxt;
    uint16_t lp  = conn->local_port;
    uint32_t rip = conn->remote_ip;
    uint16_t rp  = conn->remote_port;
    conn->state  = TCP_ST_FIN_WAIT_1;
    spinlock_unlock(&conn->lock);

    (void)tcp_send_segment(iface, iface->ipv4_addr, lp,
                           rip, rp, dst_mac,
                           snd, rcv,
                           BSD_TH_FIN | BSD_TH_ACK, NULL, 0);
    conn->snd_nxt++;
    return 0;
}

void tcp_engine_release(tcp_conn_t* conn)
{
    if (!conn) {
        return;
    }
    tcp_conn_remove(conn->local_port, conn->remote_ip, conn->remote_port);
    kfree(conn);
}

/* =========================================================================
 * Input path
 * ======================================================================= */

/* Handle an inbound SYN on a (possibly) listening port. */
static int tcp_input_syn(fabric_netif_t* iface,
                         const bsd_ether_header_t* eh,
                         uint32_t src_ip, uint16_t sport,
                         uint32_t dst_ip, uint16_t dport,
                         uint32_t seq)
{
    const net_proto_sink_t* sink = net_proto_sink();
    uint32_t backlog = 0;
    uint32_t queued = 0;

    if (!sink || !sink->tcp_listen_query ||
        sink->tcp_listen_query(dport, &backlog, &queued) != 0) {
        /* No listener — refuse the connection. */
        (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                               eh->ether_shost, 0, seq + 1u,
                               BSD_TH_RST | BSD_TH_ACK, NULL, 0);
        return 0;
    }

    if (queued >= backlog) {
        return 0; /* drop silently */
    }

    tcp_conn_t* c = (tcp_conn_t*)kmalloc(sizeof(tcp_conn_t));
    if (!c) {
        return -1;
    }
    memset(c, 0, sizeof(*c));
    c->remote_ip   = src_ip;
    c->remote_port = sport;
    c->local_port  = dport;
    c->rcv_nxt     = seq + 1u;            /* SYN consumes one seq */
    c->snd_nxt     = 0x12345678u;         /* initial ISN */
    c->state       = TCP_ST_SYN_RCVD;
    spinlock_init(&c->lock);
    c->next = NULL;

    tcp_conn_insert(c);

    (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                           eh->ether_shost,
                           c->snd_nxt,      /* seq = our ISN */
                           c->rcv_nxt,      /* ack = peer's SYN + 1 */
                           BSD_TH_SYN | BSD_TH_ACK,
                           NULL, 0);
    c->snd_nxt++;  /* SYN-ACK consumes one seq */
    return 0;
}

int tcp_input(const uint8_t* frame, uint32_t frame_len,
              const bsd_ether_header_t* eh, const bsd_ip_t* ip)
{
    (void)frame;

    const size_t ihl = (size_t)(ip->ip_vhl & 0x0Fu) * 4u;
    const size_t eth_len = sizeof(bsd_ether_header_t);

    if (frame_len < eth_len + ihl + sizeof(bsd_tcphdr_t)) {
        return -1;
    }

    const bsd_tcphdr_t* th = (const bsd_tcphdr_t*)((const uint8_t*)ip + ihl);
    uint16_t sport  = bsd_ntohs(th->th_sport);
    uint16_t dport  = bsd_ntohs(th->th_dport);
    uint32_t seq    = bsd_ntohl(th->th_seq);
    uint32_t ack_no = bsd_ntohl(th->th_ack);
    uint8_t  flags  = th->th_flags;
    uint8_t  th_off_words = (uint8_t)(th->th_off >> 4);
    size_t   tcp_hdr_len  = (size_t)th_off_words * 4u;

    uint32_t src_ip  = bsd_ntohl(ip->ip_src);
    uint32_t dst_ip  = bsd_ntohl(ip->ip_dst);
    uint16_t ip_total = bsd_ntohs(ip->ip_len);

    if (tcp_hdr_len < sizeof(bsd_tcphdr_t) ||
        frame_len < eth_len + ihl + tcp_hdr_len) {
        return -1;
    }

    if (dport == 0) {
        return -1;
    }

    size_t tcp_payload_len = 0;
    if (ip_total > (uint16_t)(ihl + tcp_hdr_len)) {
        tcp_payload_len = (size_t)ip_total - ihl - tcp_hdr_len;
    }
    const uint8_t* tcp_data = (const uint8_t*)ip + ihl + tcp_hdr_len;

    fabric_netif_t* iface = net_select_wire_iface(dst_ip);
    if (!iface) {
        return -1;
    }

    /* RST: drop any matching connection */
    if (flags & BSD_TH_RST) {
        tcp_conn_remove(dport, src_ip, sport);
        return 0;
    }

    tcp_conn_t* conn = tcp_conn_find(dport, src_ip, sport);

    /* ---- SYN: new connection request on a listening port ---- */
    if ((flags & BSD_TH_SYN) && !(flags & BSD_TH_ACK)) {
        return tcp_input_syn(iface, eh, src_ip, sport, dst_ip, dport, seq);
    }

    if (!conn) {
        return -1;
    }

    spinlock_lock(&conn->lock);

    /* SYN_RCVD: final ACK of the three-way handshake */
    if (conn->state == TCP_ST_SYN_RCVD && (flags & BSD_TH_ACK)) {
        if (ack_no != conn->snd_nxt) {
            spinlock_unlock(&conn->lock);
            return -1;
        }
        conn->state = TCP_ST_ESTABLISHED;
        spinlock_unlock(&conn->lock);

        /* Hand the established connection to the listening socket. */
        const net_proto_sink_t* sink = net_proto_sink();
        if (sink && sink->tcp_accept_enqueue) {
            spinlock_lock(&g_tcp_lock);
            tcp_conn_remove_locked(dport, src_ip, sport);
            spinlock_unlock(&g_tcp_lock);

            if (sink->tcp_accept_enqueue(dport, conn) != 0) {
                kfree(conn);
            }
        }
        return 0;
    }

    /* SYN_SENT: we sent SYN, received SYN-ACK — complete the handshake */
    if (conn->state == TCP_ST_SYN_SENT &&
        (flags & (BSD_TH_SYN | BSD_TH_ACK)) == (BSD_TH_SYN | BSD_TH_ACK)) {
        if (ack_no != conn->snd_nxt) {
            spinlock_unlock(&conn->lock);
            return -1;
        }
        conn->rcv_nxt = seq + 1u;   /* SYN consumes one sequence number */
        conn->state   = TCP_ST_ESTABLISHED;
        spinlock_unlock(&conn->lock);

        (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                               eh->ether_shost,
                               conn->snd_nxt, conn->rcv_nxt,
                               BSD_TH_ACK, NULL, 0);
        return 0;
    }

    if (conn->state == TCP_ST_ESTABLISHED) {
        /* Receive data */
        if (tcp_payload_len > 0) {
            size_t avail = TCP_RX_BUF_SZ - conn->rx_len;
            size_t to_copy = (tcp_payload_len < avail) ? tcp_payload_len : avail;
            if (to_copy > 0) {
                memcpy(conn->rx_buf + conn->rx_len, tcp_data, to_copy);
                conn->rx_len += (uint32_t)to_copy;
                conn->rcv_nxt += (uint32_t)to_copy;
            }
            spinlock_unlock(&conn->lock);

            (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                                   eh->ether_shost,
                                   conn->snd_nxt, conn->rcv_nxt,
                                   BSD_TH_ACK, NULL, 0);
            return 0;
        }

        /* FIN from peer */
        if (flags & BSD_TH_FIN) {
            conn->rcv_nxt++;  /* FIN consumes one seq */
            conn->state = TCP_ST_CLOSE_WAIT;
            spinlock_unlock(&conn->lock);

            (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                                   eh->ether_shost,
                                   conn->snd_nxt, conn->rcv_nxt,
                                   BSD_TH_ACK, NULL, 0);
            return 0;
        }

        spinlock_unlock(&conn->lock);
        return 0;
    }

    /* FIN_WAIT_1: we sent FIN, waiting for ACK (and maybe simultaneous FIN) */
    if (conn->state == TCP_ST_FIN_WAIT_1 && (flags & BSD_TH_ACK)) {
        if (flags & BSD_TH_FIN) {
            conn->rcv_nxt++;
            conn->state = TCP_ST_TIME_WAIT;
            spinlock_unlock(&conn->lock);
            (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                                   eh->ether_shost,
                                   conn->snd_nxt, conn->rcv_nxt,
                                   BSD_TH_ACK, NULL, 0);
        } else {
            conn->state = TCP_ST_FIN_WAIT_2;
            spinlock_unlock(&conn->lock);
        }
        return 0;
    }

    /* FIN_WAIT_2: got ACK of our FIN, waiting for peer's FIN */
    if (conn->state == TCP_ST_FIN_WAIT_2 && (flags & BSD_TH_FIN)) {
        conn->rcv_nxt++;
        conn->state = TCP_ST_TIME_WAIT;
        spinlock_unlock(&conn->lock);
        (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                               eh->ether_shost,
                               conn->snd_nxt, conn->rcv_nxt,
                               BSD_TH_ACK, NULL, 0);
        return 0;
    }

    spinlock_unlock(&conn->lock);
    return 0;
}
