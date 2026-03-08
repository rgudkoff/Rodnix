/**
 * @file e1000_net_stub.c
 * @brief Fabric e1000 PCI NIC backend (MVP: detect + publish netX)
 */

#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/fabric/service/net_service.h"
#include "../../../kernel/net/net.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include <stdbool.h>
#include <stdint.h>

#define PCI_CLASS_NETWORK 0x02u
#define INTEL_VENDOR_ID   0x8086u

#define E1000_IF_MAX 4

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

static int e1000_net_tx(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    (void)iface;
    /* MVP transport path: loop back through existing net queue. */
    return net_loopback_frame_tx(frame, len);
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
    slot->iface.ops = &ops;
    slot->iface.context = dev;

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
