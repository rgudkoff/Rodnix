/**
 * @file e1000_net_stub.c
 * @brief Fabric e1000 PCI NIC backend (MVP: detect + publish netX)
 */

#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/fabric/service/net_service.h"
#include "../../../kernel/net/net.h"
#include "../../../kernel/net/socket.h"
#include "../../../kernel/net/bsd_ether.h"
#include "../../../kernel/net/bsd_inet.h"
#include "../../../kernel/common/heap.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include <stdbool.h>
#include <stdint.h>

#define PCI_CLASS_NETWORK 0x02u
#define INTEL_VENDOR_ID   0x8086u

#define E1000_IF_MAX 4

#define QEMU_GW_IP  0x0A000202u /* 10.0.2.2 */
static const uint8_t g_qemu_gw_mac[BSD_ETHER_ADDR_LEN] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

typedef struct {
    int used;
    uint32_t index;
    fabric_device_t* dev;
    fabric_netif_t iface;
} e1000_slot_t;

static e1000_slot_t g_slots[E1000_IF_MAX];
static uint32_t g_next_index = 0;

static bool e1000_is_supported_device(uint16_t device_id)
{
    switch (device_id) {
        case 0x100Eu: /* 82540EM (QEMU classic e1000) */
        case 0x100Fu: /* 82545EM */
        case 0x10D3u: /* 82574L */
            return true;
        default:
            return false;
    }
}

static int e1000_inject_rx(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    if (!iface || !frame || len == 0) {
        return RDNX_E_INVALID;
    }
    (void)fabric_netif_rx_submit(iface, frame, len);
    return (net_ingress_frame(frame, len, iface) == 0) ? RDNX_OK : RDNX_E_GENERIC;
}

static int e1000_reply_arp(fabric_netif_t* iface,
                           const bsd_ether_header_t* rx_eh,
                           const bsd_arp_eth_ipv4_t* rx_arp)
{
    uint8_t frame[sizeof(bsd_ether_header_t) + sizeof(bsd_arp_eth_ipv4_t)] = {0};
    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    bsd_arp_eth_ipv4_t* arp = (bsd_arp_eth_ipv4_t*)(frame + sizeof(*eh));

    memcpy(eh->ether_dhost, rx_eh->ether_shost, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, g_qemu_gw_mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_ARP);

    arp->htype = bsd_htons(BSD_ARPHRD_ETHER);
    arp->ptype = bsd_htons(BSD_ETHERTYPE_IP);
    arp->hlen = BSD_ETHER_ADDR_LEN;
    arp->plen = 4;
    arp->oper = bsd_htons(BSD_ARPOP_REPLY);
    memcpy(arp->sha, g_qemu_gw_mac, BSD_ETHER_ADDR_LEN);
    arp->spa = bsd_htonl(QEMU_GW_IP);
    memcpy(arp->tha, rx_arp->sha, BSD_ETHER_ADDR_LEN);
    arp->tpa = rx_arp->spa;

    return e1000_inject_rx(iface, frame, sizeof(frame));
}

static int e1000_reply_icmp_echo(fabric_netif_t* iface,
                                 const bsd_ether_header_t* rx_eh,
                                 const bsd_ip_t* rx_ip,
                                 const bsd_icmp_echo_t* rx_icmp,
                                 size_t icmp_len)
{
    const size_t frame_len = sizeof(bsd_ether_header_t) + sizeof(bsd_ip_t) + icmp_len;
    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return RDNX_E_NOMEM;
    }
    memset(frame, 0, frame_len);

    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_dhost, rx_eh->ether_shost, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, g_qemu_gw_mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    bsd_ip_t* ip = (bsd_ip_t*)(frame + sizeof(*eh));
    ip->ip_vhl = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos = 0;
    ip->ip_len = bsd_htons((uint16_t)(sizeof(bsd_ip_t) + icmp_len));
    ip->ip_id = 0;
    ip->ip_off = bsd_htons(BSD_IP_DF);
    ip->ip_ttl = BSD_IP_TTL_DEF;
    ip->ip_p = BSD_IPPROTO_ICMP;
    ip->ip_src = bsd_htonl(QEMU_GW_IP);
    ip->ip_dst = rx_ip->ip_src;
    ip->ip_sum = 0;
    ip->ip_sum = bsd_in_cksum(ip, sizeof(*ip));

    uint8_t* icmp = frame + sizeof(*eh) + sizeof(*ip);
    memcpy(icmp, rx_icmp, icmp_len);
    bsd_icmp_echo_t* echo = (bsd_icmp_echo_t*)icmp;
    echo->type = BSD_ICMP_ECHOREPLY;
    echo->code = 0;
    echo->cksum = 0;
    echo->cksum = bsd_icmp_checksum(icmp, icmp_len);

    int rc = e1000_inject_rx(iface, frame, (uint32_t)frame_len);
    kfree(frame);
    return rc;
}

static int e1000_emulate_wire(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    if (!iface || !frame || len < sizeof(bsd_ether_header_t)) {
        return RDNX_E_INVALID;
    }

    const bsd_ether_header_t* eh = (const bsd_ether_header_t*)frame;
    uint16_t etype = bsd_ntohs(eh->ether_type);

    if (etype == BSD_ETHERTYPE_ARP) {
        if (len < sizeof(bsd_ether_header_t) + sizeof(bsd_arp_eth_ipv4_t)) {
            return RDNX_OK;
        }
        const bsd_arp_eth_ipv4_t* arp = (const bsd_arp_eth_ipv4_t*)((const uint8_t*)frame + sizeof(*eh));
        uint16_t oper = bsd_ntohs(arp->oper);
        uint32_t tpa = bsd_ntohl(arp->tpa);

        if (oper == BSD_ARPOP_REQUEST && tpa == QEMU_GW_IP) {
            return e1000_reply_arp(iface, eh, arp);
        }

        return RDNX_OK;
    }

    if (etype != BSD_ETHERTYPE_IP || len < sizeof(bsd_ether_header_t) + sizeof(bsd_ip_t)) {
        return RDNX_OK;
    }

    const bsd_ip_t* ip = (const bsd_ip_t*)((const uint8_t*)frame + sizeof(*eh));
    uint32_t dst_ip = bsd_ntohl(ip->ip_dst);

    /* Local delivery to own interface address for UDP e2e test through net0 path. */
    if (iface->ipv4_addr != 0 && dst_ip == iface->ipv4_addr) {
        return e1000_inject_rx(iface, frame, len);
    }

    if (ip->ip_p == BSD_IPPROTO_ICMP) {
        size_t ihl = (size_t)(ip->ip_vhl & 0x0Fu) * 4u;
        if (ihl < sizeof(bsd_ip_t) || len < sizeof(bsd_ether_header_t) + ihl + sizeof(bsd_icmp_echo_t)) {
            return RDNX_OK;
        }
        const bsd_icmp_echo_t* icmp = (const bsd_icmp_echo_t*)((const uint8_t*)ip + ihl);
        size_t icmp_len = len - (sizeof(bsd_ether_header_t) + ihl);

        if (dst_ip == QEMU_GW_IP && icmp->type == BSD_ICMP_ECHO) {
            return e1000_reply_icmp_echo(iface, eh, ip, icmp, icmp_len);
        }
    }

    return RDNX_OK;
}

static int e1000_net_tx(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    if (!iface || !frame || len == 0) {
        return RDNX_E_INVALID;
    }
    return e1000_emulate_wire(iface, frame, len);
}

static bool e1000_net_probe(fabric_device_t* dev)
{
    if (!dev) {
        return false;
    }
    if (dev->class_code != PCI_CLASS_NETWORK) {
        return false;
    }
    if (dev->vendor_id != INTEL_VENDOR_ID) {
        return false;
    }
    return e1000_is_supported_device(dev->device_id);
}

static int e1000_net_attach(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }
    if (fabric_net_service_init() != RDNX_OK) {
        kputs("[E1000] net service init failed\n");
        return RDNX_E_GENERIC;
    }

    e1000_slot_t* slot = NULL;
    for (uint32_t i = 0; i < E1000_IF_MAX; i++) {
        if (!g_slots[i].used) {
            slot = &g_slots[i];
            break;
        }
    }
    if (!slot) {
        return RDNX_E_BUSY;
    }

    static fabric_netif_ops_t ops = {
        .hdr = RDNX_ABI_INIT(fabric_netif_ops_t),
        .tx = e1000_net_tx
    };
    static const char* ifnames[E1000_IF_MAX] = {
        "net0", "net1", "net2", "net3"
    };

    uint32_t ifidx = g_next_index++;
    if (ifidx >= E1000_IF_MAX) {
        return RDNX_E_BUSY;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->index = ifidx;
    slot->dev = dev;
    slot->iface.hdr = RDNX_ABI_INIT(fabric_netif_t);
    slot->iface.name = ifnames[ifidx];
    slot->iface.mac[0] = 0x02;
    slot->iface.mac[1] = 0x52;
    slot->iface.mac[2] = 0x54;
    slot->iface.mac[3] = 0x00;
    slot->iface.mac[4] = 0x10;
    slot->iface.mac[5] = (uint8_t)(0x20u + ifidx);
    slot->iface.mtu = NET_MAX_PACKET;
    slot->iface.flags = FABRIC_NETIF_F_BROADCAST;
    slot->iface.ipv4_addr = (ifidx == 0) ? 0x0A00020Fu : 0;      /* 10.0.2.15 */
    slot->iface.ipv4_netmask = (ifidx == 0) ? 0xFFFFFF00u : 0;   /* /24 */
    slot->iface.ipv4_gateway = (ifidx == 0) ? QEMU_GW_IP : 0;    /* 10.0.2.2 */
    slot->iface.ops = &ops;
    slot->iface.context = &slot->iface;

    if (fabric_netif_register(&slot->iface) != RDNX_OK) {
        memset(slot, 0, sizeof(*slot));
        return RDNX_E_GENERIC;
    }

    fabric_log("[E1000] attached %s vendor=%x device=%x\n",
               slot->iface.name, dev->vendor_id, dev->device_id);
    return RDNX_OK;
}

static int e1000_net_publish(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }
    for (uint32_t i = 0; i < E1000_IF_MAX; i++) {
        if (g_slots[i].used && g_slots[i].dev == dev && g_slots[i].iface.name) {
            return fabric_publish_netif_node(g_slots[i].iface.name, dev);
        }
    }
    return RDNX_E_NOTFOUND;
}

static void e1000_net_detach(fabric_device_t* dev)
{
    (void)dev;
}

static fabric_driver_t g_driver = {
    .name = "e1000-net-stub",
    .probe = e1000_net_probe,
    .attach = e1000_net_attach,
    .publish = e1000_net_publish,
    .detach = e1000_net_detach,
    .suspend = NULL,
    .resume = NULL
};

void e1000_net_stub_init(void)
{
    int rc = fabric_driver_register(&g_driver);
    if (rc == 0) {
        kputs("[E1000] driver registered\n");
    } else {
        kputs("[E1000] driver register failed\n");
    }
}
