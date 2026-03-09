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
#include "../../../kernel/common/heap.h"
#include "../../../kernel/arch/x86_64/config.h"
#include "../../../kernel/arch/x86_64/paging.h"
#include "../../../kernel/arch/x86_64/pmm.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include "../../../include/dev/pci/pcireg.h"
#include "../../../include/dev/pci/pcivar.h"
#include "../../../third_party/freebsd/sys/dev/e1000/e1000_api.h"
#include <stdbool.h>
#include <stdint.h>

#define PCI_CLASS_NETWORK 0x02u
#define INTEL_VENDOR_ID   0x8086u

#define E1000_IF_MAX 4
#define E1000_MMIO_SIZE 0x20000u
#define E1000_MMIO_VIRT_BASE 0xFFFFFFFFC0000000ULL
#define E1000_TX_DESC_COUNT 64u
#define E1000_RX_DESC_COUNT 64u
#define E1000_RX_BUF_SIZE 2048u
#define E1000_TX_BUF_SIZE 2048u

#define QEMU_GW_IP  0x0A000202u /* 10.0.2.2 */

typedef struct {
    int used;
    uint32_t index;
    fabric_device_t* dev;
    struct e1000_osdep osdep;
    struct e1000_hw hw;
    uint64_t mmio_phys;
    uint64_t mmio_virt;
    uint32_t mmio_size;
    uint64_t tx_desc_phys;
    uint64_t rx_desc_phys;
    uint64_t tx_buf_phys;
    uint64_t rx_buf_phys;
    struct e1000_tx_desc* tx_desc;
    struct e1000_rx_desc* rx_desc;
    uint8_t* tx_buf;
    uint8_t* rx_buf;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_next;
    int hw_ready;
    fabric_netif_t iface;
} e1000_slot_t;

static e1000_slot_t g_slots[E1000_IF_MAX];
static uint32_t g_next_index = 0;

static int e1000_map_mmio(e1000_slot_t* slot, uint32_t bar0)
{
    if (!slot || (bar0 & 0x1u) != 0u) {
        return RDNX_E_INVALID;
    }

    uint64_t phys = (uint64_t)(bar0 & ~0xFu);
    uint64_t virt = E1000_MMIO_VIRT_BASE + (phys & 0x3FFFFFFFULL);
    const uint64_t flags = (uint64_t)(PTE_PRESENT | PTE_RW | PTE_PCD);

    for (uint64_t off = 0; off < E1000_MMIO_SIZE; off += 0x1000u) {
        if (paging_map_page_4kb(virt + off, phys + off, flags) != 0) {
            return RDNX_E_GENERIC;
        }
    }

    slot->mmio_phys = phys;
    slot->mmio_virt = virt;
    slot->mmio_size = E1000_MMIO_SIZE;
    return RDNX_OK;
}

static int e1000_hw_init(e1000_slot_t* slot, fabric_device_t* dev)
{
    if (!slot || !dev) {
        return RDNX_E_INVALID;
    }

    const pci_device_info_t* pci = (const pci_device_info_t*)dev->bus_private;
    if (!pci) {
        return RDNX_E_INVALID;
    }

    if (e1000_map_mmio(slot, pci->bars[0]) != RDNX_OK) {
        fabric_log("[E1000] mmio map failed bar0=%x\n", pci->bars[0]);
        return RDNX_E_GENERIC;
    }

    memset(&slot->osdep, 0, sizeof(slot->osdep));
    memset(&slot->hw, 0, sizeof(slot->hw));

    slot->osdep.mem_bus_space_tag = 0;
    slot->osdep.mem_bus_space_handle = (bus_space_handle_t)slot->mmio_virt;
    slot->osdep.mem_bus_space_size = slot->mmio_size;
    slot->osdep.flash_bus_space_tag = 0;
    slot->osdep.flash_bus_space_handle = (bus_space_handle_t)slot->mmio_virt;
    slot->osdep.io_bus_space_tag = 0;
    slot->osdep.io_bus_space_handle = (bus_space_handle_t)slot->mmio_virt;
    slot->osdep.dev = dev;
    slot->osdep.ctx = NULL;

    slot->hw.back = &slot->osdep;
    slot->hw.hw_addr = (u8*)(uintptr_t)slot->mmio_virt;
    slot->hw.flash_address = (u8*)(uintptr_t)slot->mmio_virt;
    slot->hw.vendor_id = dev->vendor_id;
    slot->hw.device_id = dev->device_id;
    slot->hw.revision_id = (uint8_t)pci_read_config(dev, 0x08u, 1);
    slot->hw.subsystem_vendor_id = (uint16_t)pci_read_config(dev, 0x2Cu, 2);
    slot->hw.subsystem_device_id = (uint16_t)pci_read_config(dev, 0x2Eu, 2);
    slot->hw.bus.pci_cmd_word = (uint16_t)pci_read_config(dev, PCIR_COMMAND, 2);

    fabric_log("[E1000] hw-prep bdf=%u:%u.%u bar0=%x mmio=%x cmd=%x rev=%x subsys=%x:%x\n",
               (unsigned)pci->bus,
               (unsigned)pci->device,
               (unsigned)pci->function,
               (unsigned)pci->bars[0],
               (unsigned)slot->mmio_virt,
               (unsigned)slot->hw.bus.pci_cmd_word,
               (unsigned)slot->hw.revision_id,
               (unsigned)slot->hw.subsystem_vendor_id,
               (unsigned)slot->hw.subsystem_device_id);

    s32 rc = e1000_setup_init_funcs(&slot->hw, true);
    if (rc != E1000_SUCCESS) {
        fabric_log("[E1000] setup_init_funcs rc=%d\n", (int)rc);
        return RDNX_E_GENERIC;
    }
    rc = e1000_get_bus_info(&slot->hw);
    fabric_log("[E1000] get_bus_info rc=%d type=%u speed=%u width=%u\n",
               (int)rc,
               (unsigned)slot->hw.bus.type,
               (unsigned)slot->hw.bus.speed,
               (unsigned)slot->hw.bus.width);
    rc = e1000_reset_hw(&slot->hw);
    if (rc != E1000_SUCCESS) {
        fabric_log("[E1000] reset_hw rc=%d status=%x\n",
                   (int)rc, (unsigned)E1000_READ_REG(&slot->hw, E1000_STATUS));
        return RDNX_E_GENERIC;
    }
    rc = e1000_init_hw(&slot->hw);
    if (rc != E1000_SUCCESS) {
        fabric_log("[E1000] init_hw rc=%d status=%x ctrl=%x\n",
                   (int)rc,
                   (unsigned)E1000_READ_REG(&slot->hw, E1000_STATUS),
                   (unsigned)E1000_READ_REG(&slot->hw, E1000_CTRL));
        return RDNX_E_GENERIC;
    }
    rc = e1000_read_mac_addr(&slot->hw);
    if (rc != E1000_SUCCESS) {
        fabric_log("[E1000] read_mac_addr rc=%d ral0=%x rah0=%x\n",
                   (int)rc,
                   (unsigned)E1000_READ_REG(&slot->hw, E1000_RAL(0)),
                   (unsigned)E1000_READ_REG(&slot->hw, E1000_RAH(0)));
        return RDNX_E_GENERIC;
    }
    fabric_log("[E1000] core init ok mac=%x:%x:%x:%x:%x:%x\n",
               (unsigned)slot->hw.mac.addr[0],
               (unsigned)slot->hw.mac.addr[1],
               (unsigned)slot->hw.mac.addr[2],
               (unsigned)slot->hw.mac.addr[3],
               (unsigned)slot->hw.mac.addr[4],
               (unsigned)slot->hw.mac.addr[5]);
    return RDNX_OK;
}

static int e1000_alloc_dma_rings(e1000_slot_t* slot)
{
    if (!slot) {
        return RDNX_E_INVALID;
    }

    const uint32_t tx_desc_bytes = E1000_TX_DESC_COUNT * (uint32_t)sizeof(struct e1000_tx_desc);
    const uint32_t rx_desc_bytes = E1000_RX_DESC_COUNT * (uint32_t)sizeof(struct e1000_rx_desc);
    const uint32_t tx_buf_bytes = E1000_TX_DESC_COUNT * E1000_TX_BUF_SIZE;
    const uint32_t rx_buf_bytes = E1000_RX_DESC_COUNT * E1000_RX_BUF_SIZE;

    const uint32_t tx_desc_pages = (tx_desc_bytes + 4095u) / 4096u;
    const uint32_t rx_desc_pages = (rx_desc_bytes + 4095u) / 4096u;
    const uint32_t tx_buf_pages = (tx_buf_bytes + 4095u) / 4096u;
    const uint32_t rx_buf_pages = (rx_buf_bytes + 4095u) / 4096u;

    slot->tx_desc_phys = pmm_alloc_pages(tx_desc_pages);
    slot->rx_desc_phys = pmm_alloc_pages(rx_desc_pages);
    slot->tx_buf_phys = pmm_alloc_pages(tx_buf_pages);
    slot->rx_buf_phys = pmm_alloc_pages(rx_buf_pages);
    if (!slot->tx_desc_phys || !slot->rx_desc_phys || !slot->tx_buf_phys || !slot->rx_buf_phys) {
        return RDNX_E_NOMEM;
    }

    slot->tx_desc = (struct e1000_tx_desc*)X86_64_PHYS_TO_VIRT(slot->tx_desc_phys);
    slot->rx_desc = (struct e1000_rx_desc*)X86_64_PHYS_TO_VIRT(slot->rx_desc_phys);
    slot->tx_buf = (uint8_t*)X86_64_PHYS_TO_VIRT(slot->tx_buf_phys);
    slot->rx_buf = (uint8_t*)X86_64_PHYS_TO_VIRT(slot->rx_buf_phys);
    if (!slot->tx_desc || !slot->rx_desc || !slot->tx_buf || !slot->rx_buf) {
        return RDNX_E_NOMEM;
    }

    memset(slot->tx_desc, 0, tx_desc_pages * 4096u);
    memset(slot->rx_desc, 0, rx_desc_pages * 4096u);
    memset(slot->tx_buf, 0, tx_buf_pages * 4096u);
    memset(slot->rx_buf, 0, rx_buf_pages * 4096u);

    slot->tx_count = E1000_TX_DESC_COUNT;
    slot->rx_count = E1000_RX_DESC_COUNT;
    slot->rx_next = 0;

    for (uint32_t i = 0; i < slot->tx_count; i++) {
        slot->tx_desc[i].buffer_addr = (uint64_t)(slot->tx_buf_phys + (uint64_t)i * E1000_TX_BUF_SIZE);
        slot->tx_desc[i].upper.data = E1000_TXD_STAT_DD;
    }
    for (uint32_t i = 0; i < slot->rx_count; i++) {
        slot->rx_desc[i].buffer_addr = (uint64_t)(slot->rx_buf_phys + (uint64_t)i * E1000_RX_BUF_SIZE);
        slot->rx_desc[i].status = 0;
    }

    return RDNX_OK;
}

static int e1000_hw_enable_io(e1000_slot_t* slot)
{
    if (!slot || !slot->dev) {
        return RDNX_E_INVALID;
    }
    uint16_t cmd = (uint16_t)pci_read_config(slot->dev, PCIR_COMMAND, 2);
    cmd = (uint16_t)(cmd | 0x0006u); /* bus-master + mem-space */
    pci_write_config(slot->dev, PCIR_COMMAND, cmd, 2);
    return RDNX_OK;
}

static int e1000_hw_setup_queues(e1000_slot_t* slot)
{
    if (!slot) {
        return RDNX_E_INVALID;
    }

    if (e1000_hw_enable_io(slot) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    if (e1000_alloc_dma_rings(slot) != RDNX_OK) {
        return RDNX_E_NOMEM;
    }

    E1000_WRITE_REG(&slot->hw, E1000_IMC, 0xFFFFFFFFu);
    (void)E1000_READ_REG(&slot->hw, E1000_ICR);

    E1000_WRITE_REG(&slot->hw, E1000_TDBAL(0), (uint32_t)(slot->tx_desc_phys & 0xFFFFFFFFu));
    E1000_WRITE_REG(&slot->hw, E1000_TDBAH(0), (uint32_t)(slot->tx_desc_phys >> 32));
    E1000_WRITE_REG(&slot->hw, E1000_TDLEN(0), slot->tx_count * (uint32_t)sizeof(struct e1000_tx_desc));
    E1000_WRITE_REG(&slot->hw, E1000_TDH(0), 0);
    E1000_WRITE_REG(&slot->hw, E1000_TDT(0), 0);

    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP |
                    (0x10u << E1000_CT_SHIFT) |
                    (0x40u << E1000_COLD_SHIFT);
    E1000_WRITE_REG(&slot->hw, E1000_TCTL, tctl);

    E1000_WRITE_REG(&slot->hw, E1000_RDBAL(0), (uint32_t)(slot->rx_desc_phys & 0xFFFFFFFFu));
    E1000_WRITE_REG(&slot->hw, E1000_RDBAH(0), (uint32_t)(slot->rx_desc_phys >> 32));
    E1000_WRITE_REG(&slot->hw, E1000_RDLEN(0), slot->rx_count * (uint32_t)sizeof(struct e1000_rx_desc));
    E1000_WRITE_REG(&slot->hw, E1000_RDH(0), 0);
    E1000_WRITE_REG(&slot->hw, E1000_RDT(0), slot->rx_count - 1u);

    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC | E1000_RCTL_SZ_2048;
    E1000_WRITE_REG(&slot->hw, E1000_RCTL, rctl);
    (void)E1000_READ_REG(&slot->hw, E1000_STATUS);
    return RDNX_OK;
}

static int e1000_poll_rx_slot(e1000_slot_t* slot, fabric_netif_t* iface)
{
    if (!slot || !iface || !slot->hw_ready || !slot->rx_desc || !slot->rx_buf) {
        return RDNX_E_INVALID;
    }

    int delivered = 0;
    for (uint32_t budget = 0; budget < slot->rx_count; budget++) {
        struct e1000_rx_desc* d = &slot->rx_desc[slot->rx_next];
        if ((d->status & E1000_RXD_STAT_DD) == 0) {
            break;
        }

        uint16_t len = d->length;
        if ((d->status & E1000_RXD_STAT_EOP) && len > 0 && len <= E1000_RX_BUF_SIZE) {
            uint8_t* frame = slot->rx_buf + ((uint64_t)slot->rx_next * E1000_RX_BUF_SIZE);
            (void)fabric_netif_rx_submit(iface, frame, len);
            (void)net_ingress_frame(frame, len, iface);
            delivered++;
        }

        d->status = 0;
        d->errors = 0;
        d->length = 0;
        E1000_WRITE_REG(&slot->hw, E1000_RDT(0), slot->rx_next);
        slot->rx_next = (slot->rx_next + 1u) % slot->rx_count;
    }
    return delivered;
}

static int e1000_tx_slot(e1000_slot_t* slot, const void* frame, uint32_t len)
{
    if (!slot || !frame || len == 0 || len > E1000_TX_BUF_SIZE || !slot->hw_ready) {
        return RDNX_E_INVALID;
    }

    uint32_t tail = E1000_READ_REG(&slot->hw, E1000_TDT(0));
    if (tail >= slot->tx_count) {
        return RDNX_E_GENERIC;
    }

    struct e1000_tx_desc* d = &slot->tx_desc[tail];
    if ((d->upper.data & E1000_TXD_STAT_DD) == 0) {
        return RDNX_E_BUSY;
    }

    uint8_t* txb = slot->tx_buf + ((uint64_t)tail * E1000_TX_BUF_SIZE);
    memcpy(txb, frame, len);
    d->lower.data = (len & 0xFFFFu) | E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    d->upper.data = 0;

    tail = (tail + 1u) % slot->tx_count;
    E1000_WRITE_REG(&slot->hw, E1000_TDT(0), tail);

    uint32_t spins = 200000u;
    while ((d->upper.data & E1000_TXD_STAT_DD) == 0u && spins--) {
        __asm__ volatile("pause");
    }
    (void)e1000_poll_rx_slot(slot, &slot->iface);
    return ((d->upper.data & E1000_TXD_STAT_DD) != 0u) ? RDNX_OK : RDNX_E_TIMEOUT;
}

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
    if (!iface || !frame || len == 0 || len > E1000_TX_BUF_SIZE) {
        return RDNX_E_INVALID;
    }
    e1000_slot_t* slot = (e1000_slot_t*)iface->context;
    if (!slot) {
        return RDNX_E_INVALID;
    }
    return e1000_tx_slot(slot, frame, len);
}

static int e1000_net_poll(fabric_netif_t* iface)
{
    if (!iface) {
        return RDNX_E_INVALID;
    }
    e1000_slot_t* slot = (e1000_slot_t*)iface->context;
    if (!slot) {
        return RDNX_E_INVALID;
    }
    return e1000_poll_rx_slot(slot, iface);
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
        .tx = e1000_net_tx,
        .poll = e1000_net_poll
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
    if (e1000_hw_init(slot, dev) == RDNX_OK) {
        if (e1000_hw_setup_queues(slot) == RDNX_OK) {
            slot->hw_ready = 1;
        } else {
            slot->hw_ready = 0;
            kputs("[E1000] queue setup failed\n");
        }
        for (uint32_t i = 0; i < 6u; i++) {
            slot->iface.mac[i] = slot->hw.mac.addr[i];
        }
    } else {
        slot->iface.mac[0] = 0x02;
        slot->iface.mac[1] = 0x52;
        slot->iface.mac[2] = 0x54;
        slot->iface.mac[3] = 0x00;
        slot->iface.mac[4] = 0x10;
        slot->iface.mac[5] = (uint8_t)(0x20u + ifidx);
        kputs("[E1000] freebsd core init failed, using fallback MAC\n");
    }
    slot->iface.mtu = NET_MAX_PACKET;
    slot->iface.flags = FABRIC_NETIF_F_BROADCAST;
    slot->iface.ipv4_addr = (ifidx == 0) ? 0x0A00020Fu : 0;      /* 10.0.2.15 */
    slot->iface.ipv4_netmask = (ifidx == 0) ? 0xFFFFFF00u : 0;   /* /24 */
    slot->iface.ipv4_gateway = (ifidx == 0) ? QEMU_GW_IP : 0;    /* 10.0.2.2 */
    slot->iface.ops = &ops;
    slot->iface.context = slot;

    if (fabric_netif_register(&slot->iface) != RDNX_OK) {
        memset(slot, 0, sizeof(*slot));
        return RDNX_E_GENERIC;
    }

    fabric_log("[E1000] attached %s vendor=%x device=%x hw=%s\n",
               slot->iface.name, dev->vendor_id, dev->device_id,
               slot->hw_ready ? "real" : "fallback");
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
