#include "socket.h"
#include "bsd_inet.h"
#include "bsd_ether.h"
#include "bsd_ifnet.h"
#include "bsd_mbuf.h"
#include "bsd_netisr.h"
#include "stack.h"
#include "../fabric/service/net_service.h"
#include "../fabric/spin.h"
#include "../lib/heap.h"
#include "../sched/scheduler.h"
#include "../../include/console.h"
#include "../../include/common.h"
#include "../../include/error.h"

#define NET_MAX_SOCKETS 65536
#define NET_PING_ID 0x524Eu

typedef struct udp_msg {
    size_t frame_len;
    uint8_t* frame;
    struct udp_msg* next;
} udp_msg_t;

typedef struct udp_queue {
    udp_msg_t* head;
    udp_msg_t* tail;
    uint32_t count;
    spinlock_t lock;
} udp_queue_t;

/* =========================================================================
 * TCP connection state machine
 * ======================================================================= */

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

typedef struct net_socket {
    int domain;
    int type;
    int protocol;
    uint16_t bound_port;
    int bound;
    uint16_t connected_port;
    int connected;
    udp_queue_t queue;

    /* TCP server state */
    int         listening;
    uint32_t    accept_backlog;   /* max queue size */
    tcp_conn_t* accept_head;
    tcp_conn_t* accept_tail;
    uint32_t    accept_count;
    spinlock_t  accept_lock;

    /* TCP connection (once accepted / connected) */
    tcp_conn_t* tcp_conn;
} net_socket_t;

typedef struct ping_state {
    spinlock_t lock;
    uint16_t seq;
    uint16_t await_seq;
    uint32_t await_dst;
    uint64_t start_tick;
    uint32_t last_rtt_ms;
    int waiting;
    int received;
} ping_state_t;

static const uint8_t g_mac_loopback[BSD_ETHER_ADDR_LEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

static net_socket_t* udp_port_table[NET_MAX_SOCKETS];
static spinlock_t udp_port_lock;

/* TCP: listening sockets indexed by local port */
static net_socket_t* g_tcp_listen_table[NET_MAX_SOCKETS];
/* TCP: half-open (SYN_RCVD) or ESTABLISHED connections indexed by local port */
static tcp_conn_t*   g_tcp_conn_list[NET_MAX_SOCKETS];  /* linked via ->next */
static spinlock_t    g_tcp_lock;

static int g_net_link_inited = 0;
static int g_netisr_hooked = 0;
static bsd_ifnet_t g_lo_ifp;
static ping_state_t g_ping;

static void netisr_ip_handler(bsd_mbuf_t* m);
static int  tcp_send_segment(fabric_netif_t* iface,
                             uint32_t src_ip, uint16_t src_port,
                             uint32_t dst_ip, uint16_t dst_port,
                             const uint8_t dst_mac[BSD_ETHER_ADDR_LEN],
                             uint32_t seq, uint32_t ack,
                             uint8_t flags,
                             const void* data, size_t data_len);

static const bsd_netisr_handler_t g_ip_netisr_handler = {
    .nh_name = "ip",
    .nh_handler = netisr_ip_handler,
    .nh_proto = BSD_NETISR_IP,
    .nh_qlimit = BSD_NETISR_QDEPTH,
};

static void udp_queue_init(udp_queue_t* q);
static int udp_queue_push_frame(udp_queue_t* q, const void* frame, size_t frame_len);
static int net_ensure_dispatch_path(void);

static uint64_t ticks_to_ms(uint64_t ticks)
{
    return ticks * (uint64_t)SCHEDULER_TIME_SLICE_MS;
}

static uint32_t tcp_gen_isn(uint32_t src_ip, uint16_t src_port,
                             uint32_t dst_ip, uint16_t dst_port)
{
    uint64_t t = scheduler_get_ticks();
    return (uint32_t)(t * 6364136223846793005ULL
                      ^ ((uint64_t)src_ip << 16 | src_port)
                      ^ ((uint64_t)dst_ip << 16 | dst_port));
}

static uint16_t tcp_alloc_ephemeral_port(void)
{
    static uint16_t g_eph_next = 49152u;
    spinlock_lock(&g_tcp_lock);
    for (uint32_t i = 0; i < (65535u - 49152u); i++) {
        uint16_t p = g_eph_next++;
        if (g_eph_next == 0) {
            g_eph_next = 49152u;
        }
        if (!g_tcp_conn_list[p] && !g_tcp_listen_table[p]) {
            spinlock_unlock(&g_tcp_lock);
            return p;
        }
    }
    spinlock_unlock(&g_tcp_lock);
    return 0;
}

static int net_is_local_ipv4(uint32_t ip, fabric_netif_t** out_iface)
{
    if (out_iface) {
        *out_iface = NULL;
    }

    uint32_t n = fabric_netif_count();
    for (uint32_t i = 0; i < n; i++) {
        fabric_netif_t* iface = fabric_netif_get(i);
        if (!iface || (iface->flags & FABRIC_NETIF_F_UP) == 0 || iface->ipv4_addr == 0) {
            continue;
        }
        if (iface->ipv4_addr == ip) {
            if (out_iface) {
                *out_iface = iface;
            }
            return 1;
        }
    }
    return 0;
}

static fabric_netif_t* select_tx_iface(uint32_t dst_host)
{
    uint32_t n = fabric_netif_count();
    fabric_netif_t* first_up = NULL;

    for (uint32_t i = 0; i < n; i++) {
        fabric_netif_t* iface = fabric_netif_get(i);
        if (!iface) {
            continue;
        }
        if ((iface->flags & FABRIC_NETIF_F_UP) == 0) {
            continue;
        }
        if (!first_up) {
            first_up = iface;
        }
        if (dst_host == NET_LOOPBACK_ADDR && (iface->flags & FABRIC_NETIF_F_LOOPBACK)) {
            return iface;
        }
        if (dst_host != NET_LOOPBACK_ADDR && (iface->flags & FABRIC_NETIF_F_LOOPBACK) == 0) {
            return iface;
        }
    }

    return first_up;
}

static int net_resolve_arp(fabric_netif_t* iface, uint32_t target_ip, uint8_t mac_out[BSD_ETHER_ADDR_LEN])
{
    if (!iface || !mac_out) {
        return -1;
    }
    net_stack_route_t route;
    if (net_stack_route_lookup(target_ip, &route) != RDNX_OK) {
        return -1;
    }
    route.iface = iface;
    route.src_host = ((iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0)
                     ? NET_LOOPBACK_ADDR
                     : iface->ipv4_addr;
    route.next_hop_host = target_ip;
    if ((iface->flags & FABRIC_NETIF_F_LOOPBACK) == 0 &&
        iface->ipv4_netmask != 0 && iface->ipv4_gateway != 0) {
        uint32_t src_net = route.src_host & iface->ipv4_netmask;
        uint32_t dst_net = target_ip & iface->ipv4_netmask;
        if (src_net != dst_net) {
            route.next_hop_host = iface->ipv4_gateway;
        }
    }
    if (net_stack_neighbor_resolve(&route) != RDNX_OK) {
        return -1;
    }
    memcpy(mac_out, route.dst_mac, BSD_ETHER_ADDR_LEN);
    return 0;
}

static int net_send_icmp_echo_reply(fabric_netif_t* iface,
                                    const bsd_ether_header_t* rx_eh,
                                    const bsd_ip_t* rx_ip,
                                    const bsd_icmp_echo_t* rx_icmp,
                                    size_t icmp_len)
{
    if (!iface || !rx_eh || !rx_ip || !rx_icmp || icmp_len < sizeof(bsd_icmp_echo_t)) {
        return -1;
    }

    const size_t eth_len = sizeof(bsd_ether_header_t);
    const size_t ip_len = sizeof(bsd_ip_t);
    const size_t frame_len = eth_len + ip_len + icmp_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return -1;
    }
    memset(frame, 0, frame_len);

    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_dhost, rx_eh->ether_shost, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, iface->mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    bsd_ip_t* ip = (bsd_ip_t*)(frame + eth_len);
    ip->ip_vhl = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos = 0;
    ip->ip_len = bsd_htons((uint16_t)(ip_len + icmp_len));
    ip->ip_id = 0;
    ip->ip_off = bsd_htons(BSD_IP_DF);
    ip->ip_ttl = BSD_IP_TTL_DEF;
    ip->ip_p = BSD_IPPROTO_ICMP;
    ip->ip_src = rx_ip->ip_dst;
    ip->ip_dst = rx_ip->ip_src;
    ip->ip_sum = 0;
    ip->ip_sum = bsd_htons(bsd_in_cksum(ip, sizeof(*ip)));

    uint8_t* icmp = frame + eth_len + ip_len;
    memcpy(icmp, rx_icmp, icmp_len);
    bsd_icmp_echo_t* echo = (bsd_icmp_echo_t*)icmp;
    echo->type = BSD_ICMP_ECHOREPLY;
    echo->code = 0;
    echo->cksum = 0;
    echo->cksum = bsd_htons(bsd_icmp_checksum(icmp, icmp_len));

    int rc = fabric_netif_tx(iface, frame, (uint32_t)frame_len);
    kfree(frame);
    return (rc == RDNX_OK) ? 0 : -1;
}

static int net_send_arp_reply(fabric_netif_t* iface,
                              const uint8_t peer_mac[BSD_ETHER_ADDR_LEN],
                              uint32_t peer_ip,
                              uint32_t local_ip)
{
    if (!iface || !peer_mac || local_ip == 0) {
        return -1;
    }
    net_stack_route_t route = {0};
    route.iface = iface;
    route.src_host = local_ip;
    return (net_stack_arp_reply(&route, peer_ip, peer_mac) == RDNX_OK) ? 0 : -1;
}

static void net_link_init_once(void)
{
    if (g_net_link_inited) {
        return;
    }

    bsd_arp_init();
    spinlock_init(&g_tcp_lock);
    spinlock_init(&udp_port_lock);

    memset(&g_lo_ifp, 0, sizeof(g_lo_ifp));
    memcpy(g_lo_ifp.if_xname, "lo0", 4);
    g_lo_ifp.if_flags = BSD_IFF_UP | BSD_IFF_RUNNING | BSD_IFF_LOOPBACK;
    g_lo_ifp.if_mtu = 1500;
    memcpy(g_lo_ifp.if_lladdr, g_mac_loopback, sizeof(g_lo_ifp.if_lladdr));
    (void)bsd_ifnet_attach(&g_lo_ifp);

    (void)bsd_arp_add(NET_LOOPBACK_ADDR, g_mac_loopback);

    spinlock_init(&g_ping.lock);
    g_ping.seq = 0;
    g_ping.await_seq = 0;
    g_ping.await_dst = 0;
    g_ping.start_tick = 0;
    g_ping.last_rtt_ms = 0;
    g_ping.waiting = 0;
    g_ping.received = 0;

    g_net_link_inited = 1;
}

/* =========================================================================
 * TCP helpers
 * ======================================================================= */

static tcp_conn_t* tcp_conn_find(uint16_t local_port,
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

static void tcp_conn_insert(tcp_conn_t* conn)
{
    spinlock_lock(&g_tcp_lock);
    conn->next = g_tcp_conn_list[conn->local_port];
    g_tcp_conn_list[conn->local_port] = conn;
    spinlock_unlock(&g_tcp_lock);
}

static void tcp_conn_remove(uint16_t local_port,
                             uint32_t remote_ip, uint16_t remote_port)
{
    spinlock_lock(&g_tcp_lock);
    tcp_conn_remove_locked(local_port, remote_ip, remote_port);
    spinlock_unlock(&g_tcp_lock);
}

/*
 * Build and transmit a TCP segment.
 * data/data_len may be NULL/0 for control-only segments.
 */
static int tcp_send_segment(fabric_netif_t* iface,
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

/*
 * Process an incoming TCP segment (called from netisr_ip_handler).
 * `frame` points to the raw Ethernet frame, `ip` to the IP header.
 * Returns 0 if handled, -1 to ignore.
 */
static int tcp_input(const uint8_t* frame, uint32_t frame_len,
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

    /* Compute data length */
    size_t tcp_payload_len = 0;
    if (ip_total > (uint16_t)(ihl + tcp_hdr_len)) {
        tcp_payload_len = (size_t)ip_total - ihl - tcp_hdr_len;
    }
    const uint8_t* tcp_data = (const uint8_t*)ip + ihl + tcp_hdr_len;

    /* Select outbound interface — same one the packet came in on or any UP */
    fabric_netif_t* iface = NULL;
    {
        uint32_t n = fabric_netif_count();
        for (uint32_t i = 0; i < n; i++) {
            fabric_netif_t* f = fabric_netif_get(i);
            if (!f || !(f->flags & FABRIC_NETIF_F_UP)) {
                continue;
            }
            if (f->flags & FABRIC_NETIF_F_LOOPBACK) {
                continue;
            }
            if (iface == NULL || f->ipv4_addr == dst_ip) {
                iface = f;
            }
        }
    }
    if (!iface) {
        return -1;
    }

    /* RST: remove connection if we have one */
    if (flags & BSD_TH_RST) {
        tcp_conn_remove(dport, src_ip, sport);
        return 0;
    }

    tcp_conn_t* conn = tcp_conn_find(dport, src_ip, sport);

    /* ---- SYN: new connection request on a listening port ---- */
    if ((flags & BSD_TH_SYN) && !(flags & BSD_TH_ACK)) {
        spinlock_lock(&g_tcp_lock);
        net_socket_t* lsock = g_tcp_listen_table[dport];
        spinlock_unlock(&g_tcp_lock);

        if (!lsock || !lsock->listening) {
            /* Send RST if no listener */
            (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                                   eh->ether_shost, 0, seq + 1u,
                                   BSD_TH_RST | BSD_TH_ACK, NULL, 0);
            return 0;
        }

        /* Check accept backlog */
        spinlock_lock(&lsock->accept_lock);
        uint32_t queue_len = lsock->accept_count;
        spinlock_unlock(&lsock->accept_lock);
        if (queue_len >= lsock->accept_backlog) {
            return 0; /* drop silently */
        }

        /* Allocate a new connection */
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

        /* Send SYN-ACK */
        (void)tcp_send_segment(iface, dst_ip, dport, src_ip, sport,
                               eh->ether_shost,
                               c->snd_nxt,      /* seq = our ISN */
                               c->rcv_nxt,      /* ack = peer's SYN + 1 */
                               BSD_TH_SYN | BSD_TH_ACK,
                               NULL, 0);
        c->snd_nxt++;  /* SYN-ACK consumes one seq */
        return 0;
    }

    /* ---- ACK: completing handshake or data acknowledgment ---- */
    if (!conn) {
        return -1;
    }

    spinlock_lock(&conn->lock);

    if (conn->state == TCP_ST_SYN_RCVD && (flags & BSD_TH_ACK)) {
        /* Three-way handshake complete — move to ESTABLISHED */
        if (ack_no != conn->snd_nxt) {
            spinlock_unlock(&conn->lock);
            return -1;
        }
        conn->state = TCP_ST_ESTABLISHED;
        spinlock_unlock(&conn->lock);

        /* Move connection to listen socket's accept queue */
        spinlock_lock(&g_tcp_lock);
        net_socket_t* lsock = g_tcp_listen_table[dport];
        spinlock_unlock(&g_tcp_lock);

        if (lsock) {
            spinlock_lock(&g_tcp_lock);
            tcp_conn_remove_locked(dport, src_ip, sport);
            spinlock_unlock(&g_tcp_lock);

            spinlock_lock(&lsock->accept_lock);
            conn->next = NULL;
            if (!lsock->accept_tail) {
                lsock->accept_head = conn;
                lsock->accept_tail = conn;
            } else {
                lsock->accept_tail->next = conn;
                lsock->accept_tail = conn;
            }
            lsock->accept_count++;
            spinlock_unlock(&lsock->accept_lock);
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

            /* Send ACK */
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
            /* Simultaneous close or FIN+ACK piggybacked */
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

/* =========================================================================
 * TCP socket API
 * ======================================================================= */

int net_socket_listen(net_socket_t* sock, int backlog)
{
    if (!sock || sock->type != SOCK_STREAM || !sock->bound) {
        return -1;
    }
    if (backlog <= 0) {
        backlog = 8;
    }

    spinlock_lock(&g_tcp_lock);
    g_tcp_listen_table[sock->bound_port] = sock;
    spinlock_unlock(&g_tcp_lock);

    sock->accept_backlog = (uint32_t)backlog;
    sock->listening = 1;
    return 0;
}

net_socket_t* net_socket_accept(net_socket_t* listen_sock,
                                 sockaddr_in_t* addr_out,
                                 uint64_t timeout_ms)
{
    if (!listen_sock || !listen_sock->listening) {
        return NULL;
    }

    uint64_t deadline = 0;
    if (timeout_ms > 0) {
        uint64_t ticks = (timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1))
                         / SCHEDULER_TIME_SLICE_MS;
        deadline = scheduler_get_ticks() + ticks;
    }

    for (;;) {
        spinlock_lock(&listen_sock->accept_lock);
        tcp_conn_t* conn = listen_sock->accept_head;
        if (conn) {
            listen_sock->accept_head = conn->next;
            if (!listen_sock->accept_head) {
                listen_sock->accept_tail = NULL;
            }
            listen_sock->accept_count--;
            conn->next = NULL;
        }
        spinlock_unlock(&listen_sock->accept_lock);

        if (conn) {
            /* Allocate a new socket wrapping this connection */
            net_socket_t* new_sock = (net_socket_t*)kmalloc(sizeof(net_socket_t));
            if (!new_sock) {
                kfree(conn);
                return NULL;
            }
            memset(new_sock, 0, sizeof(*new_sock));
            new_sock->domain     = AF_INET;
            new_sock->type       = SOCK_STREAM;
            new_sock->bound_port = conn->local_port;
            new_sock->bound      = 1;
            new_sock->connected  = 1;
            new_sock->tcp_conn   = conn;
            udp_queue_init(&new_sock->queue);
            spinlock_init(&new_sock->accept_lock);

            if (addr_out) {
                addr_out->sin_family = AF_INET;
                addr_out->sin_port   = conn->remote_port;
                addr_out->sin_addr   = conn->remote_ip;
            }
            return new_sock;
        }

        if (deadline && scheduler_get_ticks() >= deadline) {
            return NULL;
        }
        fabric_netif_poll_all();
        scheduler_yield();
    }
}

int net_socket_tcp_recv(net_socket_t* sock, void* buf, size_t len,
                         uint64_t timeout_ms)
{
    if (!sock || !buf || len == 0) {
        return -1;
    }
    if (sock->type != SOCK_STREAM || !sock->tcp_conn) {
        return -1;
    }

    tcp_conn_t* conn = sock->tcp_conn;

    uint64_t deadline = 0;
    if (timeout_ms > 0) {
        uint64_t ticks = (timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1))
                         / SCHEDULER_TIME_SLICE_MS;
        deadline = scheduler_get_ticks() + ticks;
    }

    for (;;) {
        spinlock_lock(&conn->lock);
        if (conn->rx_len > 0) {
            size_t to_copy = (conn->rx_len < len) ? conn->rx_len : len;
            memcpy(buf, conn->rx_buf, to_copy);
            /* Shift remaining data */
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

        if (deadline && scheduler_get_ticks() >= deadline) {
            return -1;
        }
        fabric_netif_poll_all();
        scheduler_yield();
    }
}

int net_socket_tcp_send(net_socket_t* sock, const void* buf, size_t len)
{
    if (!sock || !buf || len == 0) {
        return -1;
    }
    if (sock->type != SOCK_STREAM || !sock->tcp_conn) {
        return -1;
    }

    tcp_conn_t* conn = sock->tcp_conn;

    spinlock_lock(&conn->lock);
    if (conn->state != TCP_ST_ESTABLISHED) {
        spinlock_unlock(&conn->lock);
        return -1;
    }
    spinlock_unlock(&conn->lock);

    /* Kernel-local fast path: copy directly into peer's rx_buf */
    if (conn->is_local) {
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

    /* Select TX interface */
    fabric_netif_t* iface = NULL;
    {
        uint32_t n = fabric_netif_count();
        for (uint32_t i = 0; i < n; i++) {
            fabric_netif_t* f = fabric_netif_get(i);
            if (!f || !(f->flags & FABRIC_NETIF_F_UP)) {
                continue;
            }
            if (!(f->flags & FABRIC_NETIF_F_LOOPBACK)) {
                iface = f;
                break;
            }
        }
    }
    if (!iface) {
        return -1;
    }

    /* Resolve ARP */
    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    uint32_t arp_target = conn->remote_ip;
    if (iface->ipv4_netmask && iface->ipv4_gateway) {
        uint32_t src_net = iface->ipv4_addr & iface->ipv4_netmask;
        uint32_t dst_net = conn->remote_ip & iface->ipv4_netmask;
        if (src_net != dst_net) {
            arp_target = iface->ipv4_gateway;
        }
    }
    if (bsd_arp_lookup(arp_target, dst_mac) != 0) {
        return -1;
    }

    /* Send in MSS-sized chunks */
    const uint8_t* p = (const uint8_t*)buf;
    size_t remaining = len;
    int sent = 0;

    while (remaining > 0) {
        size_t chunk = (remaining < TCP_TX_BUF_SZ) ? remaining : TCP_TX_BUF_SZ;
        uint8_t tx_flags = BSD_TH_ACK | BSD_TH_PSH;
        if (remaining == chunk) {
            tx_flags |= BSD_TH_PSH;  /* already set; last chunk */
        }

        int rc = tcp_send_segment(iface,
                                  iface->ipv4_addr, conn->local_port,
                                  conn->remote_ip,  conn->remote_port,
                                  dst_mac,
                                  conn->snd_nxt, conn->rcv_nxt,
                                  tx_flags,
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

static void netisr_ip_handler(bsd_mbuf_t* m)
{
    if (!m) {
        return;
    }

    if (m->m_len < sizeof(bsd_ether_header_t)) {
        bsd_m_freem(m);
        return;
    }

    const uint8_t* frame = bsd_mtod(m, const uint8_t*);
    const bsd_ether_header_t* eh = (const bsd_ether_header_t*)frame;
    uint16_t etype = bsd_ntohs(eh->ether_type);
    if (etype == BSD_ETHERTYPE_ARP) {
        if (m->m_len < sizeof(bsd_ether_header_t) + sizeof(bsd_arp_eth_ipv4_t)) {
            bsd_m_freem(m);
            return;
        }

        const bsd_arp_eth_ipv4_t* arp = (const bsd_arp_eth_ipv4_t*)(frame + sizeof(*eh));
        if (bsd_ntohs(arp->htype) != BSD_ARPHRD_ETHER ||
            bsd_ntohs(arp->ptype) != BSD_ETHERTYPE_IP ||
            arp->hlen != BSD_ETHER_ADDR_LEN ||
            arp->plen != 4) {
            bsd_m_freem(m);
            return;
        }

        uint32_t spa = bsd_ntohl(arp->spa);
        uint32_t tpa = bsd_ntohl(arp->tpa);
        (void)bsd_arp_add(spa, arp->sha);

        if (bsd_ntohs(arp->oper) == BSD_ARPOP_REQUEST) {
            fabric_netif_t* local_iface = NULL;
            if (net_is_local_ipv4(tpa, &local_iface) && local_iface) {
                (void)net_send_arp_reply(local_iface, arp->sha, spa, tpa);
            }
        }

        bsd_m_freem(m);
        return;
    }

    if (etype != BSD_ETHERTYPE_IP) {
        bsd_m_freem(m);
        return;
    }

    if (m->m_len < sizeof(bsd_ether_header_t) + sizeof(bsd_ip_t)) {
        bsd_m_freem(m);
        return;
    }

    const bsd_ip_t* ip = (const bsd_ip_t*)(frame + sizeof(bsd_ether_header_t));
    const size_t ihl = (size_t)(ip->ip_vhl & 0x0Fu) * 4u;
    if (ihl < sizeof(bsd_ip_t) || m->m_len < sizeof(bsd_ether_header_t) + ihl) {
        bsd_m_freem(m);
        return;
    }

    if (bsd_in_cksum(ip, ihl) != 0) {
        bsd_m_freem(m);
        return;
    }

    if (ip->ip_p == BSD_IPPROTO_UDP) {
        if (m->m_len < sizeof(bsd_ether_header_t) + ihl + sizeof(bsd_udphdr_t)) {
            bsd_m_freem(m);
            return;
        }

        const bsd_udphdr_t* uh = (const bsd_udphdr_t*)((const uint8_t*)ip + ihl);
        const uint16_t dport = bsd_ntohs(uh->uh_dport);
        if (dport == 0) {
            bsd_m_freem(m);
            return;
        }

        net_socket_t* dest = NULL;
        spinlock_lock(&udp_port_lock);
        dest = udp_port_table[dport];
        spinlock_unlock(&udp_port_lock);

        if (dest) {
            (void)udp_queue_push_frame(&dest->queue, frame, m->m_len);
        }
        bsd_m_freem(m);
        return;
    }

    if (ip->ip_p == BSD_IPPROTO_TCP) {
        (void)tcp_input(frame, (uint32_t)m->m_len, eh, ip);
        bsd_m_freem(m);
        return;
    }

    if (ip->ip_p == BSD_IPPROTO_ICMP) {
        size_t icmp_off = sizeof(bsd_ether_header_t) + ihl;
        if (m->m_len < icmp_off + sizeof(bsd_icmp_echo_t)) {
            bsd_m_freem(m);
            return;
        }

        size_t icmp_len = m->m_len - icmp_off;
        const bsd_icmp_echo_t* icmp = (const bsd_icmp_echo_t*)(frame + icmp_off);
        if (bsd_icmp_checksum(icmp, icmp_len) != 0) {
            bsd_m_freem(m);
            return;
        }

        uint32_t src_ip = bsd_ntohl(ip->ip_src);
        uint32_t dst_ip = bsd_ntohl(ip->ip_dst);

        if (icmp->type == BSD_ICMP_ECHO) {
            fabric_netif_t* local_iface = NULL;
            if (net_is_local_ipv4(dst_ip, &local_iface) && local_iface) {
                (void)net_send_icmp_echo_reply(local_iface, eh, ip, icmp, icmp_len);
            }
        } else if (icmp->type == BSD_ICMP_ECHOREPLY) {
            uint16_t id = bsd_ntohs(icmp->id);
            uint16_t seq = bsd_ntohs(icmp->seq);

            spinlock_lock(&g_ping.lock);
            if (g_ping.waiting && id == NET_PING_ID && seq == g_ping.await_seq && src_ip == g_ping.await_dst) {
                uint64_t now = scheduler_get_ticks();
                uint64_t dt = (now >= g_ping.start_tick) ? (now - g_ping.start_tick) : 0;
                g_ping.last_rtt_ms = (uint32_t)ticks_to_ms(dt);
                g_ping.received = 1;
                g_ping.waiting = 0;
            }
            spinlock_unlock(&g_ping.lock);
        }

        bsd_m_freem(m);
        return;
    }

    bsd_m_freem(m);
}

static int net_ensure_dispatch_path(void)
{
    net_link_init_once();

    if (g_netisr_hooked) {
        return 0;
    }

    (void)bsd_netisr_init();
    if (bsd_netisr_register(&g_ip_netisr_handler) != 0) {
        return -1;
    }
    g_netisr_hooked = 1;
    return 0;
}

int net_ingress_frame(const void* frame, uint32_t len, void* ifp_hint)
{
    if (!frame || len == 0 || len > BSD_MBUF_DATA_MAX) {
        return -1;
    }
    if (net_ensure_dispatch_path() != 0) {
        return -1;
    }

    bsd_mbuf_t* m = bsd_m_gethdr(BSD_M_NOWAIT, BSD_MT_DATA);
    if (!m) {
        return -1;
    }
    m->m_pkthdr.rcvif = ifp_hint;
    if (bsd_m_append(m, frame, len) != 0) {
        bsd_m_freem(m);
        return -1;
    }
    if (bsd_netisr_dispatch(BSD_NETISR_IP, m) != 0) {
        bsd_m_freem(m);
        return -1;
    }
    return 0;
}

static void udp_queue_init(udp_queue_t* q)
{
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    spinlock_init(&q->lock);
}

static int udp_queue_push_frame(udp_queue_t* q, const void* frame, size_t frame_len)
{
    if (!q || !frame || frame_len == 0) {
        return -1;
    }

    udp_msg_t* msg = (udp_msg_t*)kmalloc(sizeof(udp_msg_t));
    if (!msg) {
        return -1;
    }
    msg->frame = (uint8_t*)kmalloc(frame_len);
    if (!msg->frame) {
        kfree(msg);
        return -1;
    }
    memcpy(msg->frame, frame, frame_len);
    msg->frame_len = frame_len;
    msg->next = NULL;

    spinlock_lock(&q->lock);
    if (!q->tail) {
        q->head = msg;
        q->tail = msg;
    } else {
        q->tail->next = msg;
        q->tail = msg;
    }
    q->count++;
    spinlock_unlock(&q->lock);

    return 0;
}

static int udp_queue_pop(udp_queue_t* q, void* buf, size_t len, sockaddr_in_t* src)
{
    if (!q || !buf || len == 0) {
        return -1;
    }

    spinlock_lock(&q->lock);
    udp_msg_t* msg = q->head;
    if (!msg) {
        spinlock_unlock(&q->lock);
        return -1;
    }
    q->head = msg->next;
    if (!q->head) {
        q->tail = NULL;
    }
    if (q->count > 0) {
        q->count--;
    }
    spinlock_unlock(&q->lock);

    if (msg->frame_len < sizeof(bsd_ether_header_t) + sizeof(bsd_ip_t) + sizeof(bsd_udphdr_t)) {
        kfree(msg->frame);
        kfree(msg);
        return -1;
    }

    const bsd_ether_header_t* eh = (const bsd_ether_header_t*)msg->frame;
    if (bsd_ntohs(eh->ether_type) != BSD_ETHERTYPE_IP) {
        kfree(msg->frame);
        kfree(msg);
        return -1;
    }

    const bsd_ip_t* ip = (const bsd_ip_t*)(msg->frame + sizeof(bsd_ether_header_t));
    const uint8_t ihl_words = (uint8_t)(ip->ip_vhl & 0x0Fu);
    const size_t ihl = (size_t)ihl_words * 4u;
    if (ihl < sizeof(bsd_ip_t) || msg->frame_len < sizeof(bsd_ether_header_t) + ihl + sizeof(bsd_udphdr_t)) {
        kfree(msg->frame);
        kfree(msg);
        return -1;
    }

    if (bsd_in_cksum(ip, ihl) != 0) {
        kfree(msg->frame);
        kfree(msg);
        return -1;
    }

    const bsd_udphdr_t* uh = (const bsd_udphdr_t*)((const uint8_t*)ip + ihl);
    const uint16_t udp_len = bsd_ntohs(uh->uh_ulen);
    if (udp_len < sizeof(bsd_udphdr_t) || sizeof(bsd_ether_header_t) + ihl + udp_len > msg->frame_len) {
        kfree(msg->frame);
        kfree(msg);
        return -1;
    }

    const uint8_t* payload = (const uint8_t*)(uh + 1);
    const size_t payload_len = (size_t)udp_len - sizeof(bsd_udphdr_t);

    if (uh->uh_sum != 0) {
        uint16_t udp_sum_calc = bsd_udp4_checksum(bsd_ntohl(ip->ip_src),
                                                  bsd_ntohl(ip->ip_dst),
                                                  uh,
                                                  payload,
                                                  payload_len);
        if (bsd_htons(udp_sum_calc) != uh->uh_sum) {
            kfree(msg->frame);
            kfree(msg);
            return -1;
        }
    }

    size_t to_copy = (payload_len < len) ? payload_len : len;
    memcpy(buf, payload, to_copy);

    if (src) {
        src->sin_family = AF_INET;
        src->sin_port = bsd_ntohs(uh->uh_sport);
        src->sin_addr = bsd_ntohl(ip->ip_src);
    }

    kfree(msg->frame);
    kfree(msg);
    return (int)to_copy;
}

static uint64_t udp_deadline(uint64_t timeout_ms)
{
    if (timeout_ms == 0) {
        return 0;
    }
    uint64_t now = scheduler_get_ticks();
    uint64_t ticks = (timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1)) / SCHEDULER_TIME_SLICE_MS;
    if (ticks == 0) {
        ticks = 1;
    }
    return now + ticks;
}

net_socket_t* net_socket_create(int domain, int type, int protocol)
{
    if (net_ensure_dispatch_path() != 0) {
        return NULL;
    }

    if (domain != AF_INET) {
        return NULL;
    }
    if (type != SOCK_DGRAM && type != SOCK_STREAM) {
        return NULL;
    }

    net_socket_t* sock = (net_socket_t*)kmalloc(sizeof(net_socket_t));
    if (!sock) {
        return NULL;
    }

    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->bound_port = 0;
    sock->bound = 0;
    sock->connected_port = 0;
    sock->connected = 0;
    sock->listening = 0;
    sock->accept_backlog = 0;
    sock->accept_head = NULL;
    sock->accept_tail = NULL;
    sock->accept_count = 0;
    sock->tcp_conn = NULL;
    spinlock_init(&sock->accept_lock);
    udp_queue_init(&sock->queue);

    return sock;
}

int net_socket_bind(net_socket_t* sock, const sockaddr_in_t* addr)
{
    if (!sock || !addr) {
        return -1;
    }
    if (sock->domain != AF_INET || addr->sin_family != AF_INET) {
        return -1;
    }

    uint16_t port = addr->sin_port;
    if (port == 0) {
        return -1;
    }

    if (sock->type == SOCK_DGRAM) {
        spinlock_lock(&udp_port_lock);
        if (udp_port_table[port]) {
            spinlock_unlock(&udp_port_lock);
            return -1;
        }
        udp_port_table[port] = sock;
        spinlock_unlock(&udp_port_lock);
    }

    sock->bound_port = port;
    sock->bound = 1;

    return 0;
}

int net_socket_connect(net_socket_t* sock, const sockaddr_in_t* addr)
{
    if (!sock || !addr) {
        return -1;
    }
    if (sock->domain != AF_INET || sock->type != SOCK_STREAM) {
        return -1;
    }
    if (addr->sin_family != AF_INET) {
        return -1;
    }

    uint16_t dport = addr->sin_port;
    if (dport == 0) {
        return -1;
    }

    /* Check if destination is a local address — kernel-local fast path */
    int is_local = net_is_local_ipv4(addr->sin_addr, NULL);
    if (is_local) {
        spinlock_lock(&g_tcp_lock);
        net_socket_t* server = g_tcp_listen_table[dport];
        spinlock_unlock(&g_tcp_lock);

        if (!server || !server->listening) {
            return -1;
        }

        /* Allocate connection pair */
        tcp_conn_t* srv_conn = (tcp_conn_t*)kmalloc(sizeof(tcp_conn_t));
        tcp_conn_t* cli_conn = (tcp_conn_t*)kmalloc(sizeof(tcp_conn_t));
        if (!srv_conn || !cli_conn) {
            kfree(srv_conn);
            kfree(cli_conn);
            return -1;
        }
        memset(srv_conn, 0, sizeof(*srv_conn));
        memset(cli_conn, 0, sizeof(*cli_conn));

        /* Client's ephemeral port (use a hash of the sock pointer) */
        uint16_t cli_port = (uint16_t)(((uintptr_t)sock >> 3) & 0xFFFFu);
        if (cli_port < 1024u) {
            cli_port += 1024u;
        }

        srv_conn->local_port  = dport;
        srv_conn->remote_port = cli_port;
        srv_conn->remote_ip   = 0x7F000001u;  /* 127.0.0.1 */
        srv_conn->state       = TCP_ST_ESTABLISHED;
        srv_conn->is_local    = 1;
        srv_conn->peer        = cli_conn;
        spinlock_init(&srv_conn->lock);

        cli_conn->local_port  = cli_port;
        cli_conn->remote_port = dport;
        cli_conn->remote_ip   = addr->sin_addr;
        cli_conn->state       = TCP_ST_ESTABLISHED;
        cli_conn->is_local    = 1;
        cli_conn->peer        = srv_conn;
        spinlock_init(&cli_conn->lock);

        /* Enqueue server-side connection into accept queue */
        spinlock_lock(&server->accept_lock);
        srv_conn->next = NULL;
        if (!server->accept_tail) {
            server->accept_head = srv_conn;
            server->accept_tail = srv_conn;
        } else {
            server->accept_tail->next = srv_conn;
            server->accept_tail = srv_conn;
        }
        server->accept_count++;
        spinlock_unlock(&server->accept_lock);

        /* Assign client-side connection to this socket */
        sock->tcp_conn      = cli_conn;
        sock->connected     = 1;
        sock->connected_port = dport;
        return 0;
    }

    /* Remote TCP connect: SYN → SYN-ACK → ACK */
    fabric_netif_t* iface = select_tx_iface(addr->sin_addr);
    if (!iface) {
        return -1;
    }

    uint16_t cli_port = tcp_alloc_ephemeral_port();
    if (cli_port == 0) {
        return -1;
    }

    tcp_conn_t* c = (tcp_conn_t*)kmalloc(sizeof(tcp_conn_t));
    if (!c) {
        return -1;
    }
    memset(c, 0, sizeof(*c));
    c->remote_ip   = addr->sin_addr;
    c->remote_port = dport;
    c->local_port  = cli_port;
    c->snd_nxt     = tcp_gen_isn(iface->ipv4_addr, cli_port, addr->sin_addr, dport);
    c->rcv_nxt     = 0;
    c->state       = TCP_ST_SYN_SENT;
    c->is_local    = 0;
    spinlock_init(&c->lock);
    tcp_conn_insert(c);

    /* Resolve next-hop MAC */
    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    uint32_t arp_target = addr->sin_addr;
    if (iface->ipv4_netmask && iface->ipv4_gateway) {
        uint32_t src_net = iface->ipv4_addr & iface->ipv4_netmask;
        uint32_t dst_net = addr->sin_addr  & iface->ipv4_netmask;
        if (src_net != dst_net) {
            arp_target = iface->ipv4_gateway;
        }
    }
    if (net_resolve_arp(iface, arp_target, dst_mac) != 0) {
        tcp_conn_remove(cli_port, addr->sin_addr, dport);
        kfree(c);
        return -1;
    }

    /* Send SYN */
    (void)tcp_send_segment(iface, iface->ipv4_addr, cli_port,
                           addr->sin_addr, dport, dst_mac,
                           c->snd_nxt, 0, BSD_TH_SYN, NULL, 0);
    c->snd_nxt++;

    /* Poll until handshake completes or timeout */
    uint64_t deadline = scheduler_get_ticks()
                        + (TCP_CONNECT_TIMEOUT_MS + (SCHEDULER_TIME_SLICE_MS - 1u))
                          / SCHEDULER_TIME_SLICE_MS;
    for (;;) {
        spinlock_lock(&c->lock);
        uint8_t st = c->state;
        spinlock_unlock(&c->lock);

        if (st == TCP_ST_ESTABLISHED) {
            sock->tcp_conn       = c;
            sock->connected      = 1;
            sock->connected_port = dport;
            sock->bound_port     = cli_port;
            sock->bound          = 1;
            return 0;
        }

        if (scheduler_get_ticks() >= deadline) {
            tcp_conn_remove(cli_port, addr->sin_addr, dport);
            kfree(c);
            return -1;
        }

        fabric_netif_poll_all();
        scheduler_yield();
    }
}

static int net_tx_params(fabric_netif_t* tx_iface,
                         uint32_t dst_host,
                         uint32_t* src_host_out,
                         uint8_t src_mac_out[BSD_ETHER_ADDR_LEN],
                         uint8_t dst_mac_out[BSD_ETHER_ADDR_LEN])
{
    if (!tx_iface || !src_host_out || !src_mac_out || !dst_mac_out) {
        return -1;
    }

    uint32_t src_host = (tx_iface->flags & FABRIC_NETIF_F_LOOPBACK) ? NET_LOOPBACK_ADDR : tx_iface->ipv4_addr;
    if (src_host == 0) {
        return -1;
    }

    memcpy(src_mac_out, tx_iface->mac, BSD_ETHER_ADDR_LEN);

    if ((tx_iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0) {
        memcpy(dst_mac_out, g_mac_loopback, BSD_ETHER_ADDR_LEN);
        *src_host_out = src_host;
        return 0;
    }

    if (tx_iface->ipv4_addr != 0 && dst_host == tx_iface->ipv4_addr) {
        memcpy(dst_mac_out, tx_iface->mac, BSD_ETHER_ADDR_LEN);
        *src_host_out = src_host;
        return 0;
    }

    uint32_t arp_target = dst_host;
    if (tx_iface->ipv4_netmask != 0 && tx_iface->ipv4_gateway != 0) {
        uint32_t src_net = src_host & tx_iface->ipv4_netmask;
        uint32_t dst_net = dst_host & tx_iface->ipv4_netmask;
        if (src_net != dst_net) {
            arp_target = tx_iface->ipv4_gateway;
        }
    }

    if (net_resolve_arp(tx_iface, arp_target, dst_mac_out) != 0) {
        return -1;
    }

    *src_host_out = src_host;
    return 0;
}

int net_socket_sendto(net_socket_t* sock, const void* buf, size_t len, const sockaddr_in_t* dst)
{
    if (!sock || !buf || !dst) {
        return -1;
    }
    if (len == 0 || len > (size_t)(0xFFFFu - sizeof(bsd_udphdr_t))) {
        return -1;
    }
    if (sock->domain != AF_INET || (sock->type != SOCK_DGRAM && sock->type != SOCK_STREAM)) {
        return -1;
    }
    if (dst->sin_family != AF_INET || dst->sin_addr == 0) {
        return -1;
    }

    uint16_t dport = dst->sin_port;
    if (dport == 0) {
        return -1;
    }

    uint32_t dst_host = dst->sin_addr;
    fabric_netif_t* tx_iface = select_tx_iface(dst_host);
    if (!tx_iface) {
        return -1;
    }

    uint32_t src_host = 0;
    uint8_t src_mac[BSD_ETHER_ADDR_LEN];
    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    if (net_tx_params(tx_iface, dst_host, &src_host, src_mac, dst_mac) != 0) {
        return -1;
    }

    const size_t eth_len = sizeof(bsd_ether_header_t);
    const size_t ip_len = sizeof(bsd_ip_t);
    const size_t udp_len = sizeof(bsd_udphdr_t) + len;
    const size_t frame_len = eth_len + ip_len + udp_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return -1;
    }
    memset(frame, 0, frame_len);

    uint16_t sport = sock->bound ? sock->bound_port : 0;

    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_shost, src_mac, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_dhost, dst_mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    bsd_ip_t* ip = (bsd_ip_t*)(frame + eth_len);
    ip->ip_vhl = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos = 0;
    ip->ip_len = bsd_htons((uint16_t)(ip_len + udp_len));
    ip->ip_id = 0;
    ip->ip_off = bsd_htons(BSD_IP_DF);
    ip->ip_ttl = BSD_IP_TTL_DEF;
    ip->ip_p = BSD_IPPROTO_UDP;
    ip->ip_src = bsd_htonl(src_host);
    ip->ip_dst = bsd_htonl(dst_host);
    ip->ip_sum = 0;
    ip->ip_sum = bsd_htons(bsd_in_cksum(ip, sizeof(*ip)));

    bsd_udphdr_t* uh = (bsd_udphdr_t*)(frame + eth_len + ip_len);
    uh->uh_sport = bsd_htons(sport);
    uh->uh_dport = bsd_htons(dport);
    uh->uh_ulen = bsd_htons((uint16_t)udp_len);
    uh->uh_sum = 0;
    memcpy((uint8_t*)(uh + 1), buf, len);
    uh->uh_sum = bsd_htons(bsd_udp4_checksum(src_host, dst_host, uh, (const void*)(uh + 1), len));

    int tx_rc = fabric_netif_tx(tx_iface, frame, (uint32_t)frame_len);
    kfree(frame);
    return (tx_rc == RDNX_OK) ? (int)len : -1;
}

int net_socket_send(net_socket_t* sock, const void* buf, size_t len)
{
    if (!sock || !buf) {
        return -1;
    }
    if (sock->type != SOCK_STREAM || !sock->connected) {
        return -1;
    }

    sockaddr_in_t dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_addr = NET_LOOPBACK_ADDR;
    dst.sin_port = sock->connected_port;
    return net_socket_sendto(sock, buf, len, &dst);
}

int net_socket_recvfrom(net_socket_t* sock, void* buf, size_t len, sockaddr_in_t* src, uint64_t timeout_ms)
{
    if (!sock || !buf) {
        return -1;
    }
    if (sock->domain != AF_INET || (sock->type != SOCK_DGRAM && sock->type != SOCK_STREAM)) {
        return -1;
    }

    uint64_t deadline = udp_deadline(timeout_ms);
    for (;;) {
        int ret = udp_queue_pop(&sock->queue, buf, len, src);
        if (ret >= 0) {
            return ret;
        }
        if (deadline && scheduler_get_ticks() >= deadline) {
            return -1;
        }
        fabric_netif_poll_all();
        scheduler_yield();
    }
}

int net_socket_recv(net_socket_t* sock, void* buf, size_t len, uint64_t timeout_ms)
{
    return net_socket_recvfrom(sock, buf, len, NULL, timeout_ms);
}

void net_socket_close(net_socket_t* sock)
{
    if (!sock) {
        return;
    }

    if (sock->listening) {
        spinlock_lock(&g_tcp_lock);
        if (g_tcp_listen_table[sock->bound_port] == sock) {
            g_tcp_listen_table[sock->bound_port] = NULL;
        }
        spinlock_unlock(&g_tcp_lock);

        /* Free queued connections */
        spinlock_lock(&sock->accept_lock);
        tcp_conn_t* c = sock->accept_head;
        while (c) {
            tcp_conn_t* next = c->next;
            kfree(c);
            c = next;
        }
        spinlock_unlock(&sock->accept_lock);
    }

    if (sock->tcp_conn) {
        tcp_conn_t* conn = sock->tcp_conn;
        spinlock_lock(&conn->lock);
        uint8_t st = conn->state;

        if (!conn->is_local && (st == TCP_ST_ESTABLISHED || st == TCP_ST_CLOSE_WAIT)) {
            fabric_netif_t* iface = select_tx_iface(conn->remote_ip);
            if (iface) {
                uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
                uint32_t arp_target = conn->remote_ip;
                if (iface->ipv4_netmask && iface->ipv4_gateway) {
                    uint32_t src_net = iface->ipv4_addr & iface->ipv4_netmask;
                    uint32_t dst_net = conn->remote_ip  & iface->ipv4_netmask;
                    if (src_net != dst_net) {
                        arp_target = iface->ipv4_gateway;
                    }
                }
                if (bsd_arp_lookup(arp_target, dst_mac) == 0) {
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

                    /* Best-effort wait for graceful teardown (2 s max) */
                    uint64_t dl = scheduler_get_ticks()
                                  + 2000u / SCHEDULER_TIME_SLICE_MS;
                    for (;;) {
                        spinlock_lock(&conn->lock);
                        uint8_t s2 = conn->state;
                        spinlock_unlock(&conn->lock);
                        if (s2 == TCP_ST_TIME_WAIT) {
                            break;
                        }
                        if (scheduler_get_ticks() >= dl) {
                            break;
                        }
                        fabric_netif_poll_all();
                        scheduler_yield();
                    }
                } else {
                    spinlock_unlock(&conn->lock);
                }
            } else {
                spinlock_unlock(&conn->lock);
            }
        } else {
            spinlock_unlock(&conn->lock);
        }

        tcp_conn_remove(conn->local_port, conn->remote_ip, conn->remote_port);
        kfree(conn);
        sock->tcp_conn = NULL;
    }

    if (sock->bound && sock->type == SOCK_DGRAM) {
        spinlock_lock(&udp_port_lock);
        if (udp_port_table[sock->bound_port] == sock) {
            udp_port_table[sock->bound_port] = NULL;
        }
        spinlock_unlock(&udp_port_lock);
    }

    for (;;) {
        uint8_t tmp[1];
        if (udp_queue_pop(&sock->queue, tmp, sizeof(tmp), NULL) < 0) {
            break;
        }
    }

    kfree(sock);
}

int net_ping_ipv4(uint32_t dst_host, uint32_t timeout_ms, uint32_t* out_rtt_ms)
{
    if (dst_host == 0) {
        return -1;
    }
    if (net_ensure_dispatch_path() != 0) {
        return -1;
    }

    fabric_netif_t* tx_iface = select_tx_iface(dst_host);
    if (!tx_iface) {
        return -1;
    }

    uint32_t src_host = 0;
    uint8_t src_mac[BSD_ETHER_ADDR_LEN];
    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    if (net_tx_params(tx_iface, dst_host, &src_host, src_mac, dst_mac) != 0) {
        return -1;
    }

    uint8_t payload[16] = { 'r','o','d','n','i','x','-','p','i','n','g',0,1,2,3,4 };
    const size_t icmp_len = sizeof(bsd_icmp_echo_t) + sizeof(payload);
    const size_t frame_len = sizeof(bsd_ether_header_t) + sizeof(bsd_ip_t) + icmp_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return -1;
    }
    memset(frame, 0, frame_len);

    spinlock_lock(&g_ping.lock);
    uint16_t seq = ++g_ping.seq;
    g_ping.await_seq = seq;
    g_ping.await_dst = dst_host;
    g_ping.start_tick = scheduler_get_ticks();
    g_ping.last_rtt_ms = 0;
    g_ping.waiting = 1;
    g_ping.received = 0;
    spinlock_unlock(&g_ping.lock);

    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_shost, src_mac, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_dhost, dst_mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    bsd_ip_t* ip = (bsd_ip_t*)(frame + sizeof(*eh));
    ip->ip_vhl = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos = 0;
    ip->ip_len = bsd_htons((uint16_t)(sizeof(bsd_ip_t) + icmp_len));
    ip->ip_id = 0;
    ip->ip_off = bsd_htons(BSD_IP_DF);
    ip->ip_ttl = BSD_IP_TTL_DEF;
    ip->ip_p = BSD_IPPROTO_ICMP;
    ip->ip_src = bsd_htonl(src_host);
    ip->ip_dst = bsd_htonl(dst_host);
    ip->ip_sum = 0;
    ip->ip_sum = bsd_htons(bsd_in_cksum(ip, sizeof(*ip)));

    uint8_t* icmp_buf = frame + sizeof(*eh) + sizeof(*ip);
    bsd_icmp_echo_t* icmp = (bsd_icmp_echo_t*)icmp_buf;
    icmp->type = BSD_ICMP_ECHO;
    icmp->code = 0;
    icmp->cksum = 0;
    icmp->id = bsd_htons(NET_PING_ID);
    icmp->seq = bsd_htons(seq);
    memcpy(icmp_buf + sizeof(*icmp), payload, sizeof(payload));
    icmp->cksum = bsd_htons(bsd_icmp_checksum(icmp_buf, icmp_len));

    int tx_rc = fabric_netif_tx(tx_iface, frame, (uint32_t)frame_len);
    kfree(frame);
    if (tx_rc != RDNX_OK) {
        spinlock_lock(&g_ping.lock);
        g_ping.waiting = 0;
        spinlock_unlock(&g_ping.lock);
        return -1;
    }

    uint64_t deadline = scheduler_get_ticks() + ((timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1)) / SCHEDULER_TIME_SLICE_MS);
    if (deadline == scheduler_get_ticks()) {
        deadline++;
    }

    for (;;) {
        int done = 0;
        uint32_t rtt = 0;

        spinlock_lock(&g_ping.lock);
        if (g_ping.received && g_ping.await_seq == seq) {
            done = 1;
            rtt = g_ping.last_rtt_ms;
            g_ping.received = 0;
        }
        spinlock_unlock(&g_ping.lock);

        if (done) {
            if (out_rtt_ms) {
                *out_rtt_ms = rtt;
            }
            return 0;
        }

        if (scheduler_get_ticks() >= deadline) {
            spinlock_lock(&g_ping.lock);
            if (g_ping.await_seq == seq) {
                g_ping.waiting = 0;
            }
            spinlock_unlock(&g_ping.lock);
            return -1;
        }

        fabric_netif_poll_all();
        scheduler_yield();
    }
}
