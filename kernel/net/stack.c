#include "stack.h"
#include "bsd_inet.h"
#include "socket.h"
#include "../common/heap.h"
#include "../common/scheduler.h"
#include "../../include/common.h"

static int g_net_stack_ready = 0;

static int net_stack_arp_emit(fabric_netif_t* iface,
                              uint16_t oper,
                              const uint8_t dst_mac[BSD_ETHER_ADDR_LEN],
                              const uint8_t target_mac[BSD_ETHER_ADDR_LEN],
                              uint32_t spa_host,
                              uint32_t tpa_host)
{
    uint8_t frame[sizeof(bsd_ether_header_t) + sizeof(bsd_arp_eth_ipv4_t)] = {0};
    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    bsd_arp_eth_ipv4_t* arp = (bsd_arp_eth_ipv4_t*)(frame + sizeof(*eh));

    memcpy(eh->ether_dhost, dst_mac, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, iface->mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_ARP);

    arp->htype = bsd_htons(BSD_ARPHRD_ETHER);
    arp->ptype = bsd_htons(BSD_ETHERTYPE_IP);
    arp->hlen = BSD_ETHER_ADDR_LEN;
    arp->plen = 4;
    arp->oper = bsd_htons(oper);
    memcpy(arp->sha, iface->mac, BSD_ETHER_ADDR_LEN);
    arp->spa = bsd_htonl(spa_host);
    memcpy(arp->tha, target_mac, BSD_ETHER_ADDR_LEN);
    arp->tpa = bsd_htonl(tpa_host);

    return (fabric_netif_tx(iface, frame, sizeof(frame)) == RDNX_OK)
           ? RDNX_OK
           : RDNX_E_GENERIC;
}

static fabric_netif_t* net_stack_pick_iface(uint32_t dst_host)
{
    uint32_t n = fabric_netif_count();
    fabric_netif_t* first_up = NULL;

    for (uint32_t i = 0; i < n; i++) {
        fabric_netif_t* iface = fabric_netif_get(i);
        if (!iface || (iface->flags & FABRIC_NETIF_F_UP) == 0) {
            continue;
        }
        if (!first_up) {
            first_up = iface;
        }
        if (dst_host == NET_LOOPBACK_ADDR && (iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0) {
            return iface;
        }
        if (dst_host != NET_LOOPBACK_ADDR && (iface->flags & FABRIC_NETIF_F_LOOPBACK) == 0) {
            return iface;
        }
    }

    return first_up;
}

int net_stack_bootstrap(void)
{
    if (g_net_stack_ready) {
        return RDNX_OK;
    }

    if (fabric_net_service_init() != RDNX_OK) {
        return RDNX_E_GENERIC;
    }

    /* The cache is global and init is idempotent for our current implementation. */
    bsd_arp_init();
    g_net_stack_ready = 1;
    return RDNX_OK;
}

int net_stack_poll(void)
{
    if (net_stack_bootstrap() != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    fabric_netif_poll_all();
    return RDNX_OK;
}

int net_stack_ingress(const net_stack_frame_t* frame)
{
    if (!frame || !frame->data || frame->len == 0) {
        return RDNX_E_INVALID;
    }
    if (net_stack_bootstrap() != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    return (net_ingress_frame(frame->data, frame->len, frame->ifp_hint) == 0)
           ? RDNX_OK
           : RDNX_E_GENERIC;
}

int net_stack_route_lookup(uint32_t dst_host, net_stack_route_t* out_route)
{
    if (!out_route || dst_host == 0) {
        return RDNX_E_INVALID;
    }
    if (net_stack_bootstrap() != RDNX_OK) {
        return RDNX_E_GENERIC;
    }

    fabric_netif_t* iface = net_stack_pick_iface(dst_host);
    if (!iface) {
        return RDNX_E_NOTFOUND;
    }

    memset(out_route, 0, sizeof(*out_route));
    out_route->iface = iface;
    out_route->dst_host = dst_host;
    out_route->src_host = ((iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0)
                          ? NET_LOOPBACK_ADDR
                          : iface->ipv4_addr;
    out_route->next_hop_host = dst_host;

    if ((iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0) {
        memcpy(out_route->dst_mac, iface->mac, BSD_ETHER_ADDR_LEN);
        out_route->mac_resolved = 1;
        return RDNX_OK;
    }

    if (iface->ipv4_netmask != 0 && iface->ipv4_gateway != 0) {
        uint32_t src_net = out_route->src_host & iface->ipv4_netmask;
        uint32_t dst_net = dst_host & iface->ipv4_netmask;
        if (src_net != dst_net) {
            out_route->next_hop_host = iface->ipv4_gateway;
        }
    }

    return RDNX_OK;
}

int net_stack_neighbor_resolve(net_stack_route_t* route)
{
    static const uint8_t broadcast_mac[BSD_ETHER_ADDR_LEN] =
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    if (!route || !route->iface) {
        return RDNX_E_INVALID;
    }
    if (route->mac_resolved) {
        return RDNX_OK;
    }

    if ((route->iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0) {
        memcpy(route->dst_mac, route->iface->mac, BSD_ETHER_ADDR_LEN);
        route->mac_resolved = 1;
        return RDNX_OK;
    }
    if (route->dst_host == 0xFFFFFFFFu) {
        memcpy(route->dst_mac, broadcast_mac, BSD_ETHER_ADDR_LEN);
        route->mac_resolved = 1;
        return RDNX_OK;
    }
    if (bsd_arp_lookup(route->next_hop_host, route->dst_mac) != 0) {
        int rc = net_stack_arp_request(route->next_hop_host);
        if (rc != RDNX_OK && rc != RDNX_E_AGAIN) {
            return rc;
        }

        uint64_t deadline = scheduler_get_ticks() + 20u; /* ~200ms */
        while (scheduler_get_ticks() < deadline) {
            if (bsd_arp_lookup(route->next_hop_host, route->dst_mac) == 0) {
                route->mac_resolved = 1;
                return RDNX_OK;
            }
            fabric_netif_poll_all();
            scheduler_yield();
        }
        return RDNX_E_AGAIN;
    }

    route->mac_resolved = 1;
    return RDNX_OK;
}

int net_stack_ether_input(const net_stack_frame_t* frame)
{
    return net_stack_ingress(frame);
}

int net_stack_ether_output(net_stack_route_t* route, const void* frame, uint32_t len)
{
    if (!route || !route->iface || !frame || len == 0) {
        return RDNX_E_INVALID;
    }
    if (net_stack_neighbor_resolve(route) != RDNX_OK) {
        return RDNX_E_AGAIN;
    }
    return (fabric_netif_tx(route->iface, frame, len) == RDNX_OK)
           ? RDNX_OK
           : RDNX_E_GENERIC;
}

int net_stack_arp_input(const net_stack_frame_t* frame)
{
    return net_stack_ingress(frame);
}

int net_stack_arp_request(uint32_t target_ip)
{
    net_stack_route_t route;
    if (net_stack_route_lookup(target_ip, &route) != RDNX_OK) {
        return RDNX_E_NOTFOUND;
    }
    if (!route.iface || (route.iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0 ||
        route.src_host == 0) {
        return RDNX_E_INVALID;
    }

    static const uint8_t broadcast_mac[BSD_ETHER_ADDR_LEN] =
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t zero_mac[BSD_ETHER_ADDR_LEN] = {0, 0, 0, 0, 0, 0};

    return net_stack_arp_emit(route.iface,
                              BSD_ARPOP_REQUEST,
                              broadcast_mac,
                              zero_mac,
                              route.src_host,
                              target_ip);
}

int net_stack_arp_reply(const net_stack_route_t* route,
                        uint32_t target_ip,
                        const uint8_t target_mac[BSD_ETHER_ADDR_LEN])
{
    if (!route || !route->iface || !target_mac || route->src_host == 0) {
        return RDNX_E_INVALID;
    }

    return net_stack_arp_emit(route->iface,
                              BSD_ARPOP_REPLY,
                              target_mac,
                              target_mac,
                              route->src_host,
                              target_ip);
}

int net_stack_ipv4_input(const net_stack_frame_t* frame)
{
    return net_stack_ingress(frame);
}

int net_stack_ipv4_output(const net_stack_ipv4_msg_t* msg)
{
    if (!msg || !msg->payload || msg->payload_len == 0 || msg->dst_host == 0) {
        return RDNX_E_INVALID;
    }

    net_stack_route_t route;
    int rc = net_stack_route_lookup(msg->dst_host, &route);
    if (rc != RDNX_OK) {
        return rc;
    }
    rc = net_stack_neighbor_resolve(&route);
    if (rc != RDNX_OK) {
        return rc;
    }

    const size_t eth_len = sizeof(bsd_ether_header_t);
    const size_t ip_len = sizeof(bsd_ip_t);
    const size_t frame_len = eth_len + ip_len + msg->payload_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return RDNX_E_NOMEM;
    }
    memset(frame, 0, frame_len);

    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_dhost, route.dst_mac, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, route.iface->mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    bsd_ip_t* ip = (bsd_ip_t*)(frame + eth_len);
    ip->ip_vhl = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos = 0;
    ip->ip_len = bsd_htons((uint16_t)(ip_len + msg->payload_len));
    ip->ip_id = 0;
    ip->ip_off = bsd_htons(BSD_IP_DF);
    ip->ip_ttl = msg->ttl ? msg->ttl : BSD_IP_TTL_DEF;
    ip->ip_p = msg->protocol;
    ip->ip_src = bsd_htonl(msg->src_host ? msg->src_host : route.src_host);
    ip->ip_dst = bsd_htonl(msg->dst_host);
    ip->ip_sum = 0;
    ip->ip_sum = bsd_htons(bsd_in_cksum(ip, sizeof(*ip)));

    memcpy(frame + eth_len + ip_len, msg->payload, msg->payload_len);

    rc = net_stack_ether_output(&route, frame, (uint32_t)frame_len);
    kfree(frame);
    return rc;
}

int net_stack_ipv4_forward(const net_stack_frame_t* frame)
{
    (void)frame;
    return RDNX_E_UNSUPPORTED;
}

int net_stack_icmp_input(const net_stack_frame_t* frame)
{
    return net_stack_ingress(frame);
}

int net_stack_icmp_echo(uint32_t dst_host, uint32_t timeout_ms, uint32_t* out_rtt_ms)
{
    return net_ping_ipv4(dst_host, timeout_ms, out_rtt_ms);
}

int net_stack_udp_input(const net_stack_frame_t* frame)
{
    return net_stack_ingress(frame);
}

int net_stack_udp_output(const net_stack_udp_msg_t* msg)
{
    if (!msg || !msg->payload || msg->payload_len > (size_t)(0xFFFFu - sizeof(bsd_udphdr_t)) ||
        msg->dst_host == 0 || msg->dst_port == 0) {
        return RDNX_E_INVALID;
    }

    const size_t udp_len = sizeof(bsd_udphdr_t) + msg->payload_len;
    uint8_t* udp_buf = (uint8_t*)kmalloc(udp_len);
    if (!udp_buf) {
        return RDNX_E_NOMEM;
    }

    bsd_udphdr_t* uh = (bsd_udphdr_t*)udp_buf;
    uh->uh_sport = bsd_htons(msg->src_port);
    uh->uh_dport = bsd_htons(msg->dst_port);
    uh->uh_ulen = bsd_htons((uint16_t)udp_len);
    uh->uh_sum = 0;
    memcpy(udp_buf + sizeof(*uh), msg->payload, msg->payload_len);
    uh->uh_sum = bsd_htons(bsd_udp4_checksum(msg->src_host, msg->dst_host, uh,
                                             udp_buf + sizeof(*uh), msg->payload_len));

    net_stack_ipv4_msg_t ip_msg = {
        .src_host = msg->src_host,
        .dst_host = msg->dst_host,
        .ttl = BSD_IP_TTL_DEF,
        .protocol = BSD_IPPROTO_UDP,
        .payload = udp_buf,
        .payload_len = udp_len,
    };

    int rc = net_stack_ipv4_output(&ip_msg);
    kfree(udp_buf);
    return rc;
}

int net_stack_tcp_input(const net_stack_frame_t* frame)
{
    return net_stack_ingress(frame);
}

int net_stack_tcp_output(const net_stack_tcp_msg_t* msg)
{
    (void)msg;
    return RDNX_E_UNSUPPORTED;
}

int net_stack_tcp_listen(uint16_t port, int backlog)
{
    (void)port;
    (void)backlog;
    return RDNX_E_UNSUPPORTED;
}

int net_stack_tcp_connect(uint32_t dst_host, uint16_t dst_port, uint64_t timeout_ms)
{
    (void)dst_host;
    (void)dst_port;
    (void)timeout_ms;
    return RDNX_E_UNSUPPORTED;
}

int net_stack_tcp_close(uint16_t local_port, uint32_t remote_ip, uint16_t remote_port)
{
    (void)local_port;
    (void)remote_ip;
    (void)remote_port;
    return RDNX_E_UNSUPPORTED;
}
