#include "socket.h"
#include "bsd_inet.h"
#include "bsd_ether.h"
#include "bsd_ifnet.h"
#include "bsd_mbuf.h"
#include "bsd_netisr.h"
#include "../fabric/service/net_service.h"
#include "../fabric/spin.h"
#include "../common/heap.h"
#include "../common/scheduler.h"
#include "../../include/common.h"
#include "../../include/error.h"

#define NET_MAX_SOCKETS 1024
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

typedef struct net_socket {
    int domain;
    int type;
    int protocol;
    uint16_t bound_port;
    int bound;
    uint16_t connected_port;
    int connected;
    udp_queue_t queue;
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

static int g_net_link_inited = 0;
static int g_netisr_hooked = 0;
static bsd_ifnet_t g_lo_ifp;
static ping_state_t g_ping;

static void netisr_ip_handler(bsd_mbuf_t* m);

static const bsd_netisr_handler_t g_ip_netisr_handler = {
    .nh_name = "ip",
    .nh_handler = netisr_ip_handler,
    .nh_proto = BSD_NETISR_IP,
    .nh_qlimit = BSD_NETISR_QDEPTH,
};

static int udp_queue_push_frame(udp_queue_t* q, const void* frame, size_t frame_len);
static int net_ensure_dispatch_path(void);

static uint64_t ticks_to_ms(uint64_t ticks)
{
    return ticks * (uint64_t)SCHEDULER_TIME_SLICE_MS;
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

static int net_send_arp_request(fabric_netif_t* iface, uint32_t target_ip)
{
    if (!iface || (iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0 || iface->ipv4_addr == 0) {
        return -1;
    }

    uint8_t frame[sizeof(bsd_ether_header_t) + sizeof(bsd_arp_eth_ipv4_t)] = {0};
    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    bsd_arp_eth_ipv4_t* arp = (bsd_arp_eth_ipv4_t*)(frame + sizeof(*eh));

    memset(eh->ether_dhost, 0xFF, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, iface->mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_ARP);

    arp->htype = bsd_htons(BSD_ARPHRD_ETHER);
    arp->ptype = bsd_htons(BSD_ETHERTYPE_IP);
    arp->hlen = BSD_ETHER_ADDR_LEN;
    arp->plen = 4;
    arp->oper = bsd_htons(BSD_ARPOP_REQUEST);
    memcpy(arp->sha, iface->mac, BSD_ETHER_ADDR_LEN);
    arp->spa = bsd_htonl(iface->ipv4_addr);
    memset(arp->tha, 0, BSD_ETHER_ADDR_LEN);
    arp->tpa = bsd_htonl(target_ip);

    return (fabric_netif_tx(iface, frame, sizeof(frame)) == RDNX_OK) ? 0 : -1;
}

static int net_resolve_arp(fabric_netif_t* iface, uint32_t target_ip, uint8_t mac_out[BSD_ETHER_ADDR_LEN])
{
    if (!iface || !mac_out) {
        return -1;
    }

    if (bsd_arp_lookup(target_ip, mac_out) == 0) {
        return 0;
    }

    if (net_send_arp_request(iface, target_ip) != 0) {
        return -1;
    }

    uint64_t deadline = scheduler_get_ticks() + 20; /* ~200ms */
    while (scheduler_get_ticks() < deadline) {
        if (bsd_arp_lookup(target_ip, mac_out) == 0) {
            return 0;
        }
        fabric_netif_poll_all();
        scheduler_yield();
    }

    return -1;
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
    ip->ip_sum = bsd_in_cksum(ip, sizeof(*ip));

    uint8_t* icmp = frame + eth_len + ip_len;
    memcpy(icmp, rx_icmp, icmp_len);
    bsd_icmp_echo_t* echo = (bsd_icmp_echo_t*)icmp;
    echo->type = BSD_ICMP_ECHOREPLY;
    echo->code = 0;
    echo->cksum = 0;
    echo->cksum = bsd_icmp_checksum(icmp, icmp_len);

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

    uint8_t frame[sizeof(bsd_ether_header_t) + sizeof(bsd_arp_eth_ipv4_t)] = {0};
    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    bsd_arp_eth_ipv4_t* arp = (bsd_arp_eth_ipv4_t*)(frame + sizeof(*eh));

    memcpy(eh->ether_dhost, peer_mac, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, iface->mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_ARP);

    arp->htype = bsd_htons(BSD_ARPHRD_ETHER);
    arp->ptype = bsd_htons(BSD_ETHERTYPE_IP);
    arp->hlen = BSD_ETHER_ADDR_LEN;
    arp->plen = 4;
    arp->oper = bsd_htons(BSD_ARPOP_REPLY);
    memcpy(arp->sha, iface->mac, BSD_ETHER_ADDR_LEN);
    arp->spa = bsd_htonl(local_ip);
    memcpy(arp->tha, peer_mac, BSD_ETHER_ADDR_LEN);
    arp->tpa = bsd_htonl(peer_ip);

    return (fabric_netif_tx(iface, frame, sizeof(frame)) == RDNX_OK) ? 0 : -1;
}

static void net_link_init_once(void)
{
    if (g_net_link_inited) {
        return;
    }

    bsd_arp_init();

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

    bsd_ip_t ip_hdr;
    memcpy(&ip_hdr, ip, sizeof(ip_hdr));
    const uint16_t ip_sum_wire = ip_hdr.ip_sum;
    ip_hdr.ip_sum = 0;
    if (bsd_in_cksum(&ip_hdr, sizeof(ip_hdr)) != ip_sum_wire) {
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
        if (dport == 0 || dport >= NET_MAX_SOCKETS) {
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

    bsd_ip_t ip_hdr;
    memcpy(&ip_hdr, ip, sizeof(ip_hdr));
    const uint16_t ip_sum_wire = ip_hdr.ip_sum;
    ip_hdr.ip_sum = 0;
    if (bsd_in_cksum(&ip_hdr, sizeof(ip_hdr)) != ip_sum_wire) {
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
        if (udp_sum_calc != uh->uh_sum) {
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
    if (port == 0 || port >= NET_MAX_SOCKETS) {
        return -1;
    }

    spinlock_lock(&udp_port_lock);
    if (udp_port_table[port]) {
        spinlock_unlock(&udp_port_lock);
        return -1;
    }
    udp_port_table[port] = sock;
    sock->bound_port = port;
    sock->bound = 1;
    spinlock_unlock(&udp_port_lock);

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
    if (dport == 0 || dport >= NET_MAX_SOCKETS) {
        return -1;
    }

    spinlock_lock(&udp_port_lock);
    net_socket_t* dest = udp_port_table[dport];
    spinlock_unlock(&udp_port_lock);
    if (!dest || dest->type != SOCK_STREAM) {
        return -1;
    }

    sock->connected_port = dport;
    sock->connected = 1;
    return 0;
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
    if (dport == 0 || dport >= NET_MAX_SOCKETS) {
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
    ip->ip_sum = bsd_in_cksum(ip, sizeof(*ip));

    bsd_udphdr_t* uh = (bsd_udphdr_t*)(frame + eth_len + ip_len);
    uh->uh_sport = bsd_htons(sport);
    uh->uh_dport = bsd_htons(dport);
    uh->uh_ulen = bsd_htons((uint16_t)udp_len);
    uh->uh_sum = 0;
    memcpy((uint8_t*)(uh + 1), buf, len);
    uh->uh_sum = bsd_udp4_checksum(src_host, dst_host, uh, (const void*)(uh + 1), len);

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

    if (sock->bound) {
        spinlock_lock(&udp_port_lock);
        if (sock->bound_port < NET_MAX_SOCKETS && udp_port_table[sock->bound_port] == sock) {
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
    ip->ip_sum = bsd_in_cksum(ip, sizeof(*ip));

    uint8_t* icmp_buf = frame + sizeof(*eh) + sizeof(*ip);
    bsd_icmp_echo_t* icmp = (bsd_icmp_echo_t*)icmp_buf;
    icmp->type = BSD_ICMP_ECHO;
    icmp->code = 0;
    icmp->cksum = 0;
    icmp->id = bsd_htons(NET_PING_ID);
    icmp->seq = bsd_htons(seq);
    memcpy(icmp_buf + sizeof(*icmp), payload, sizeof(payload));
    icmp->cksum = bsd_icmp_checksum(icmp_buf, icmp_len);

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
