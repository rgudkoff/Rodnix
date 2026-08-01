/*
 * proto_ip.c — IP input dispatch, ARP handling, link bring-up, and the
 * link/route helpers shared by the protocol engine and the socket layer.
 *
 * This file owns the netisr hook: every inbound frame arrives here, is
 * validated at the Ethernet and IP layers, and is then handed to the
 * per-protocol engines (proto_tcp.c, proto_udp.c, proto_icmp.c). UDP payloads
 * reach the socket layer through net_proto_sink_t, never by direct reference.
 */

#include "net_proto.h"
#include "proto_tcp.h"
#include "proto_udp.h"
#include "proto_icmp.h"
#include "socket.h"
#include "bsd_inet.h"
#include "bsd_ether.h"
#include "bsd_ifnet.h"
#include "bsd_mbuf.h"
#include "bsd_netisr.h"
#include "stack.h"
#include "../kernel/fabric/service/net_service.h"
#include "../kernel/fabric/spin.h"
#include "../lib/heap.h"
#include "../sched/scheduler.h"
#include "../include/console.h"
#include "../include/common.h"
#include "../include/error.h"

const uint8_t net_mac_loopback[BSD_ETHER_ADDR_LEN] =
    {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

static int g_net_link_inited = 0;
static int g_netisr_hooked = 0;
static bsd_ifnet_t g_lo_ifp;

static const net_proto_sink_t* g_sink = NULL;

static void netisr_ip_handler(bsd_mbuf_t* m);

static const bsd_netisr_handler_t g_ip_netisr_handler = {
    .nh_name = "ip",
    .nh_handler = netisr_ip_handler,
    .nh_proto = BSD_NETISR_IP,
    .nh_qlimit = BSD_NETISR_QDEPTH,
};

/* =========================================================================
 * Sink registration
 * ======================================================================= */

void net_proto_set_sink(const net_proto_sink_t* sink)
{
    g_sink = sink;
}

const net_proto_sink_t* net_proto_sink(void)
{
    return g_sink;
}

/* =========================================================================
 * Time helpers
 * ======================================================================= */

uint64_t net_ticks_to_ms(uint64_t ticks)
{
    return ticks * (uint64_t)SCHEDULER_TIME_SLICE_MS;
}

uint64_t net_deadline_from_ms(uint64_t timeout_ms)
{
    if (timeout_ms == 0) {
        return 0;
    }
    uint64_t ticks = (timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1))
                     / SCHEDULER_TIME_SLICE_MS;
    if (ticks == 0) {
        ticks = 1;
    }
    return scheduler_get_ticks() + ticks;
}

/* =========================================================================
 * Interface selection and ARP
 * ======================================================================= */

int net_is_local_ipv4(uint32_t ip, fabric_netif_t** out_iface)
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

fabric_netif_t* net_select_tx_iface(uint32_t dst_host)
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

fabric_netif_t* net_select_wire_iface(uint32_t dst_ip)
{
    fabric_netif_t* iface = NULL;
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
    return iface;
}

uint32_t net_next_hop(fabric_netif_t* iface, uint32_t dst_host)
{
    if (!iface) {
        return dst_host;
    }
    if (iface->ipv4_netmask != 0 && iface->ipv4_gateway != 0) {
        uint32_t src_net = iface->ipv4_addr & iface->ipv4_netmask;
        uint32_t dst_net = dst_host & iface->ipv4_netmask;
        if (src_net != dst_net) {
            return iface->ipv4_gateway;
        }
    }
    return dst_host;
}

int net_resolve_arp(fabric_netif_t* iface, uint32_t target_ip,
                    uint8_t mac_out[BSD_ETHER_ADDR_LEN])
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

int net_tx_params(fabric_netif_t* tx_iface,
                  uint32_t dst_host,
                  uint32_t* src_host_out,
                  uint8_t src_mac_out[BSD_ETHER_ADDR_LEN],
                  uint8_t dst_mac_out[BSD_ETHER_ADDR_LEN])
{
    if (!tx_iface || !src_host_out || !src_mac_out || !dst_mac_out) {
        return -1;
    }

    uint32_t src_host = (tx_iface->flags & FABRIC_NETIF_F_LOOPBACK)
                        ? NET_LOOPBACK_ADDR : tx_iface->ipv4_addr;
    if (src_host == 0) {
        return -1;
    }

    memcpy(src_mac_out, tx_iface->mac, BSD_ETHER_ADDR_LEN);

    if ((tx_iface->flags & FABRIC_NETIF_F_LOOPBACK) != 0) {
        memcpy(dst_mac_out, net_mac_loopback, BSD_ETHER_ADDR_LEN);
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

int net_frame_emit(fabric_netif_t* iface, uint32_t dst_host,
                   const void* frame, uint32_t frame_len)
{
    if (!iface || !frame || frame_len == 0) {
        return -1;
    }

    /*
     * A frame addressed to one of our own wire addresses never comes back:
     * the NIC transmits it and nothing reflects it, so the input path never
     * sees it. Feed it straight into ingress instead.
     *
     * Loopback interfaces are excluded — lo0 already re-injects from its own
     * tx callback, and routing 127.0.0.1 through here would bypass that.
     */
    if ((iface->flags & FABRIC_NETIF_F_LOOPBACK) == 0 &&
        net_is_local_ipv4(dst_host, NULL)) {
        return (net_ingress_frame(frame, frame_len, iface) == 0) ? 0 : -1;
    }

    return (fabric_netif_tx(iface, frame, frame_len) == RDNX_OK) ? 0 : -1;
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

/* =========================================================================
 * Link bring-up
 * ======================================================================= */

void net_link_init_once(void)
{
    if (g_net_link_inited) {
        return;
    }

    bsd_arp_init();
    tcp_engine_init();

    memset(&g_lo_ifp, 0, sizeof(g_lo_ifp));
    memcpy(g_lo_ifp.if_xname, "lo0", 4);
    g_lo_ifp.if_flags = BSD_IFF_UP | BSD_IFF_RUNNING | BSD_IFF_LOOPBACK;
    g_lo_ifp.if_mtu = 1500;
    memcpy(g_lo_ifp.if_lladdr, net_mac_loopback, sizeof(g_lo_ifp.if_lladdr));
    (void)bsd_ifnet_attach(&g_lo_ifp);

    (void)bsd_arp_add(NET_LOOPBACK_ADDR, net_mac_loopback);

    icmp_proto_init();

    g_net_link_inited = 1;
}

int net_ensure_dispatch_path(void)
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

/* =========================================================================
 * Input path
 * ======================================================================= */

static void ip_handle_arp(const uint8_t* frame, const bsd_mbuf_t* m)
{
    const bsd_ether_header_t* eh = (const bsd_ether_header_t*)frame;

    if (m->m_len < sizeof(bsd_ether_header_t) + sizeof(bsd_arp_eth_ipv4_t)) {
        return;
    }

    const bsd_arp_eth_ipv4_t* arp =
        (const bsd_arp_eth_ipv4_t*)(frame + sizeof(*eh));
    if (bsd_ntohs(arp->htype) != BSD_ARPHRD_ETHER ||
        bsd_ntohs(arp->ptype) != BSD_ETHERTYPE_IP ||
        arp->hlen != BSD_ETHER_ADDR_LEN ||
        arp->plen != 4) {
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
}

static void ip_handle_icmp(const uint8_t* frame, const bsd_mbuf_t* m,
                           const bsd_ether_header_t* eh,
                           const bsd_ip_t* ip, size_t ihl)
{
    size_t icmp_off = sizeof(bsd_ether_header_t) + ihl;
    if (m->m_len < icmp_off + sizeof(bsd_icmp_echo_t)) {
        return;
    }

    size_t icmp_len = m->m_len - icmp_off;
    const bsd_icmp_echo_t* icmp = (const bsd_icmp_echo_t*)(frame + icmp_off);
    if (bsd_icmp_checksum(icmp, icmp_len) != 0) {
        return;
    }

    uint32_t src_ip = bsd_ntohl(ip->ip_src);
    uint32_t dst_ip = bsd_ntohl(ip->ip_dst);

    if (icmp->type == BSD_ICMP_ECHO) {
        fabric_netif_t* local_iface = NULL;
        if (net_is_local_ipv4(dst_ip, &local_iface) && local_iface) {
            (void)icmp_proto_send_echo_reply(local_iface, eh, ip, icmp, icmp_len);
        }
    } else if (icmp->type == BSD_ICMP_ECHOREPLY) {
        icmp_proto_note_echo_reply(src_ip,
                                   bsd_ntohs(icmp->id),
                                   bsd_ntohs(icmp->seq));
    }
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
        ip_handle_arp(frame, m);
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
        if (dport != 0 && g_sink && g_sink->udp_deliver) {
            (void)g_sink->udp_deliver(dport, frame, m->m_len);
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
        ip_handle_icmp(frame, m, eh, ip, ihl);
        bsd_m_freem(m);
        return;
    }

    bsd_m_freem(m);
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
