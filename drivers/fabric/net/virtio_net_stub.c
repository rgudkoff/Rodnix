/**
 * @file virtio_net_stub.c
 * @brief Fabric virtio-net stub driver (MVP bootstrap)
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
#define VIRTIO_VENDOR_ID  0x1AF4u

#define VIRTIO_NET_IF_MAX 4

typedef struct {
    int used;
    uint32_t index;
    fabric_device_t* dev;
    fabric_netif_t iface;
} virtio_net_slot_t;

static virtio_net_slot_t g_slots[VIRTIO_NET_IF_MAX];
static uint32_t g_next_index = 0;

static int virtio_net_tx(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    (void)iface;
    return net_loopback_frame_tx(frame, len);
}

static bool virtio_net_probe(fabric_device_t* dev)
{
    if (!dev) {
        return false;
    }
    if (dev->class_code != PCI_CLASS_NETWORK) {
        return false;
    }
    if (dev->vendor_id != VIRTIO_VENDOR_ID) {
        return false;
    }
    return true;
}

static int virtio_net_attach(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }
    if (fabric_net_service_init() != RDNX_OK) {
        kputs("[VNET] net service init failed\n");
        return RDNX_E_GENERIC;
    }

    virtio_net_slot_t* slot = NULL;
    for (uint32_t i = 0; i < VIRTIO_NET_IF_MAX; i++) {
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
        .tx = virtio_net_tx
    };
    static const char* ifnames[VIRTIO_NET_IF_MAX] = {
        "eth0", "eth1", "eth2", "eth3"
    };

    uint32_t ifidx = g_next_index++;
    if (ifidx >= VIRTIO_NET_IF_MAX) {
        return RDNX_E_BUSY;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->index = ifidx;
    slot->dev = dev;
    slot->iface.hdr = RDNX_ABI_INIT(fabric_netif_t);
    slot->iface.name = ifnames[ifidx];
    slot->iface.mac[0] = 0x02;
    slot->iface.mac[1] = 0xAA;
    slot->iface.mac[2] = 0xBB;
    slot->iface.mac[3] = 0xCC;
    slot->iface.mac[4] = 0x00;
    slot->iface.mac[5] = (uint8_t)(0x10 + ifidx);
    slot->iface.mtu = NET_MAX_PACKET;
    slot->iface.flags = FABRIC_NETIF_F_UP | FABRIC_NETIF_F_BROADCAST;
    slot->iface.ops = &ops;
    slot->iface.context = slot;

    if (fabric_netif_register(&slot->iface) != RDNX_OK) {
        memset(slot, 0, sizeof(*slot));
        return RDNX_E_GENERIC;
    }

    fabric_log("[VNET] attached %s vendor=%x device=%x\n",
               slot->iface.name, dev->vendor_id, dev->device_id);
    return RDNX_OK;
}

static void virtio_net_detach(fabric_device_t* dev)
{
    (void)dev;
}

static fabric_driver_t g_driver = {
    .name = "virtio-net-stub",
    .probe = virtio_net_probe,
    .attach = virtio_net_attach,
    .detach = virtio_net_detach,
    .suspend = NULL,
    .resume = NULL
};

void virtio_net_stub_init(void)
{
    int rc = fabric_driver_register(&g_driver);
    if (rc == 0) {
        kputs("[VNET] driver registered\n");
    } else {
        kputs("[VNET] driver register failed\n");
    }
}
