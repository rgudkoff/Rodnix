/**
 * @file e1000_net.c
 * @brief Intel 82540EM (e1000) network driver for RodNIX Fabric
 *
 * Polling-mode driver targeting QEMU -device e1000,netdev=net0.
 * Uses the I/O-indirect register access path (BAR2 IOADDR/IODATA) so no
 * MMIO mapping of the 128 KB MMIO BAR is required.
 *
 * Locking: per-device spinlock guards the TX/RX tail pointers and
 * descriptor rings.  All register I/O is serialised by the same lock.
 */

#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/fabric/service/net_service.h"
#include "../../../kernel/fabric/bus/pci.h"
#include "../../../kernel/fabric/spin.h"
#include "../../../kernel/net/socket.h"
#include "../../../kernel/core/memory.h"
#include "../../../kernel/core/interrupts.h"
#include "../../../kernel/arch/x86_64/config.h"
#include "../../../kernel/arch/x86_64/paging.h"
#include "../../../kernel/arch/x86_64/apic.h"
#include "../../../kernel/arch/x86_64/pic.h"
#include "../../../include/dev/pci/pcireg.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * PCI constants
 * ======================================================================= */
#define PCI_VENDOR_INTEL    0x8086u
#define PCI_CLASS_NETWORK   0x02u
#define PCI_CFG_ADDR        ((uint16_t)0xCF8u)
#define PCI_CFG_DATA        ((uint16_t)0xCFCu)
#define PCI_CMD_IO_EN       (1u << 0)
#define PCI_CMD_MEM_EN      (1u << 1)
#define PCI_CMD_BUS_MASTER  (1u << 2)

/* Known e1000 PCI device IDs */
static const uint16_t e1000_devids[] = {
    0x100E, /* 82540EM — default QEMU -device e1000 */
    0x100F, /* 82545EM Copper */
    0x1010, /* 82546EB Copper */
    0x1076, /* 82541GI */
    0x107C, /* 82541PI */
    0x10D3, /* 82574L */
    0x10EA, /* 82577LM */
};
#define E1000_DEVID_COUNT (sizeof(e1000_devids) / sizeof(e1000_devids[0]))
#define E1000_MMIO_SIZE   0x20000u
#define E1000_MMIO_VIRT_BASE 0xFFFFFFFFFEC00000ULL

/* =========================================================================
 * I/O port helpers
 * ======================================================================= */
static inline void e1000_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %%eax, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t e1000_inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile ("inl %1, %%eax" : "=a"(val) : "Nd"(port));
    return val;
}

/* =========================================================================
 * e1000 register offsets
 * ======================================================================= */
#define E1000_CTRL   0x00000u  /* Device Control */
#define E1000_STATUS 0x00008u  /* Device Status */
#define E1000_ICR    0x000C0u  /* Interrupt Cause Read */
#define E1000_IMC    0x000D8u  /* Interrupt Mask Clear */
#define E1000_IMS    0x000D0u  /* Interrupt Mask Set */
#define E1000_RCTL   0x00100u  /* RX Control */
#define E1000_TCTL   0x00400u  /* TX Control */
#define E1000_TIPG   0x00410u  /* TX Inter Packet Gap */
#define E1000_RDBAL  0x02800u  /* RX Desc Base Address Low */
#define E1000_RDBAH  0x02804u  /* RX Desc Base Address High */
#define E1000_RDLEN  0x02808u  /* RX Descriptor Length */
#define E1000_RDH    0x02810u  /* RX Descriptor Head */
#define E1000_RDT    0x02818u  /* RX Descriptor Tail */
#define E1000_TDBAL  0x03800u  /* TX Desc Base Address Low */
#define E1000_TDBAH  0x03804u  /* TX Desc Base Address High */
#define E1000_TDLEN  0x03808u  /* TX Descriptor Length */
#define E1000_TDH    0x03810u  /* TX Descriptor Head */
#define E1000_TDT    0x03818u  /* TX Descriptor Tail */
#define E1000_RAL0   0x05400u  /* Receive Address Low  (MAC bytes 0-3) */
#define E1000_RAH0   0x05404u  /* Receive Address High (MAC bytes 4-5) + AV */
#define E1000_MTA    0x05200u  /* Multicast Table Array (128 dwords) */

/* CTRL bits */
#define E1000_CTRL_SLU   (1u << 6)   /* Set Link Up */
#define E1000_CTRL_RST   (1u << 26)  /* Software Reset */

/* RCTL bits — BSIZE=00, BSEX=0 → 2048-byte RX buffers */
#define E1000_RCTL_EN    (1u << 1)
#define E1000_RCTL_SBP   (1u << 2)
#define E1000_RCTL_UPE   (1u << 3)
#define E1000_RCTL_MPE   (1u << 4)
#define E1000_RCTL_BAM   (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)

/* TCTL bits */
#define E1000_TCTL_EN   (1u << 1)
#define E1000_TCTL_PSP  (1u << 3)
#define E1000_TCTL_CT   (0x0Fu << 4)    /* Collision Threshold = 15 */
#define E1000_TCTL_COLD (0x40u << 12)   /* Collision Distance  = 64 */

/* TIPG: IPGT=8, IPGR1=4, IPGR2=6  (802.3 1 Gbps copper) */
#define E1000_TIPG_VALUE (8u | (4u << 10) | (6u << 20))

/* Descriptor command / status */
#define E1000_TXD_CMD_EOP  0x01u
#define E1000_TXD_CMD_IFCS 0x02u
#define E1000_TXD_CMD_RS   0x08u
#define E1000_TXD_STAT_DD  0x01u
#define E1000_RXD_STAT_DD  0x01u
#define E1000_RXD_STAT_EOP 0x02u
#define E1000_RAH_AV       (1u << 31)
#define E1000_IC_TXDW      (1u << 0)
#define E1000_IC_LSC       (1u << 2)
#define E1000_IC_RXDMT0    (1u << 4)
#define E1000_IC_RXT0      (1u << 7)

/* =========================================================================
 * Descriptor structures  (both exactly 16 bytes)
 * ======================================================================= */
#define E1000_NUM_RX  16u
#define E1000_NUM_TX  16u
#define E1000_BUF_SZ  2048u

typedef struct __attribute__((packed)) {
    uint64_t addr;      /* buffer physical address */
    uint16_t length;    /* bytes written by hardware */
    uint16_t csum;      /* packet checksum */
    uint8_t  status;    /* descriptor status (DD, EOP, …) */
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;      /* buffer physical address */
    uint16_t length;    /* bytes to transmit */
    uint8_t  cso;       /* checksum offset */
    uint8_t  cmd;       /* EOP | IFCS | RS */
    uint8_t  sta;       /* status; DD set when done */
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

/* =========================================================================
 * Per-device state
 * ======================================================================= */
#define E1000_MAX_DEVS 2u

typedef struct {
    int        used;
    uint32_t   index;
    uint16_t   io_base;           /* I/O BAR base (IOADDR at +0, IODATA at +4) */
    uint64_t   mmio_phys;
    volatile uint8_t* mmio_base;
    uint8_t    pci_dev;           /* PCI device number on bus 0 */
    uint8_t    pci_fn;            /* PCI function */

    /* RX ring */
    e1000_rx_desc_t* rx_ring;     /* 16-byte aligned */
    uint64_t         rx_ring_phys;
    uint8_t*         rx_buf[E1000_NUM_RX];
    uint64_t         rx_buf_phys[E1000_NUM_RX];
    uint32_t         rx_tail;     /* next descriptor to check for DD */

    /* TX ring */
    e1000_tx_desc_t* tx_ring;
    uint64_t         tx_ring_phys;
    uint8_t*         tx_buf[E1000_NUM_TX];
    uint64_t         tx_buf_phys[E1000_NUM_TX];
    uint32_t         tx_tail;     /* next free TX slot */
    pci_irq_binding_t irq;
    uint8_t          irq_pending;

    spinlock_t       lock;
    fabric_netif_t   iface;
    fabric_device_t* fdev;
} e1000_dev_t;

static e1000_dev_t g_devs[E1000_MAX_DEVS];
static uint32_t    g_dev_count = 0;

static void e1000_irq_handler(int vector, void* arg);

/* =========================================================================
 * Helpers
 * ======================================================================= */

static void* e1000_alloc_dma(size_t sz, uint64_t* phys)
{
    uint32_t pages = (uint32_t)ALIGN_UP(sz, PAGE_SIZE) / PAGE_SIZE;
    if (pages == 0u) {
        pages = 1u;
    }

    uint64_t dma_phys = pmm_alloc_pages(pages);
    if (!dma_phys) {
        return NULL;
    }

    if (phys) {
        *phys = dma_phys;
    }

    void* dma_virt = X86_64_PHYS_TO_VIRT(dma_phys);
    if (!dma_virt) {
        return NULL;
    }
    memset(dma_virt, 0, (size_t)pages * PAGE_SIZE);
    return dma_virt;
}

/* =========================================================================
 * Register access via I/O indirect method
 * ======================================================================= */
static uint32_t e1000_rd(e1000_dev_t* d, uint32_t reg)
{
    if (d->mmio_base) {
        return *(volatile uint32_t*)(d->mmio_base + reg);
    }
    e1000_outl(d->io_base,           reg);
    return e1000_inl((uint16_t)(d->io_base + 4u));
}

static void e1000_wr(e1000_dev_t* d, uint32_t reg, uint32_t val)
{
    if (d->mmio_base) {
        *(volatile uint32_t*)(d->mmio_base + reg) = val;
        __asm__ volatile ("" ::: "memory");
        return;
    }
    e1000_outl(d->io_base,           reg);
    e1000_outl((uint16_t)(d->io_base + 4u), val);
}

static int e1000_map_mmio(e1000_dev_t* d, uint32_t bar0)
{
    if (!d || (bar0 & 0x1u) != 0u) {
        return RDNX_E_INVALID;
    }

    uint64_t mmio_phys = (uint64_t)(bar0 & ~0xFu);
    if (mmio_phys == 0u) {
        return RDNX_E_INVALID;
    }

    uint64_t mmio_virt = E1000_MMIO_VIRT_BASE + ((uint64_t)d->index * E1000_MMIO_SIZE);
    uint64_t mmio_flags = PTE_PRESENT | PTE_RW | PTE_PCD;

    for (uint64_t off = 0; off < E1000_MMIO_SIZE; off += PAGE_SIZE) {
        if (paging_map_page_4kb(mmio_virt + off, mmio_phys + off, mmio_flags) != 0) {
            return RDNX_E_GENERIC;
        }
    }

    d->mmio_phys = mmio_phys;
    d->mmio_base = (volatile uint8_t*)(uintptr_t)mmio_virt;
    return RDNX_OK;
}

static bool e1000_mac_is_zero(const uint8_t mac[6])
{
    if (!mac) {
        return true;
    }
    for (uint32_t i = 0; i < 6u; i++) {
        if (mac[i] != 0u) {
            return false;
        }
    }
    return true;
}

static void e1000_set_hw_mac(e1000_dev_t* d, const uint8_t mac[6])
{
    if (!d || !mac) {
        return;
    }

    memcpy(d->iface.mac, mac, 6u);

    uint32_t ral = ((uint32_t)mac[0]) |
                   ((uint32_t)mac[1] << 8) |
                   ((uint32_t)mac[2] << 16) |
                   ((uint32_t)mac[3] << 24);
    uint32_t rah = ((uint32_t)mac[4]) |
                   ((uint32_t)mac[5] << 8) |
                   E1000_RAH_AV;

    e1000_wr(d, E1000_RAL0, ral);
    e1000_wr(d, E1000_RAH0, rah);
}

/* =========================================================================
 * PCI config-space write (bus 0)
 * ======================================================================= */
static void e1000_pci_cmd_set(uint8_t dev, uint8_t fn, uint32_t bits)
{
    /* Read current command register */
    uint32_t addr = (1u << 31) | ((uint32_t)dev << 11) |
                    ((uint32_t)fn << 8) | 0x04u;
    e1000_outl(PCI_CFG_ADDR, addr);
    uint32_t cur = e1000_inl(PCI_CFG_DATA);
    /* Write back with new bits */
    e1000_outl(PCI_CFG_ADDR, addr);
    e1000_outl(PCI_CFG_DATA, cur | bits);
}

/* =========================================================================
 * Fabric TX callback
 * ======================================================================= */
static int e1000_tx_cb(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    e1000_dev_t* d = (e1000_dev_t*)iface->context;
    if (!d || !frame || len == 0u || len > E1000_BUF_SZ) {
        return RDNX_E_INVALID;
    }

    spinlock_lock(&d->lock);

    uint32_t slot = d->tx_tail % E1000_NUM_TX;

    /* Poll until the slot is free (hardware sets DD when done) */
    uint32_t timeout = 500000u;
    while (d->tx_ring[slot].length != 0u &&
           !(d->tx_ring[slot].sta & E1000_TXD_STAT_DD)) {
        if (--timeout == 0u) {
            spinlock_unlock(&d->lock);
            return RDNX_E_TIMEOUT;
        }
    }

    memcpy(d->tx_buf[slot], frame, len);

    d->tx_ring[slot].addr   = d->tx_buf_phys[slot];
    d->tx_ring[slot].length = (uint16_t)len;
    d->tx_ring[slot].cso    = 0;
    d->tx_ring[slot].cmd    = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
                               E1000_TXD_CMD_RS;
    d->tx_ring[slot].sta    = 0;
    d->tx_ring[slot].css    = 0;
    d->tx_ring[slot].special = 0;

    d->tx_tail = (slot + 1u) % E1000_NUM_TX;
    e1000_wr(d, E1000_TDT, d->tx_tail);
    (void)e1000_rd(d, E1000_STATUS);

    timeout = 500000u;
    while (!(d->tx_ring[slot].sta & E1000_TXD_STAT_DD)) {
        if (--timeout == 0u) {
            spinlock_unlock(&d->lock);
            return RDNX_E_TIMEOUT;
        }
        __asm__ volatile ("pause");
    }

    spinlock_unlock(&d->lock);
    return RDNX_OK;
}

/* =========================================================================
 * Fabric poll callback — drains the RX ring
 * ======================================================================= */
static int e1000_poll_cb(fabric_netif_t* iface)
{
    e1000_dev_t* d = (e1000_dev_t*)iface->context;
    if (!d) {
        return RDNX_E_INVALID;
    }

    d->irq_pending = 0u;
    uint32_t processed = 0;
    spinlock_lock(&d->lock);

    while (d->rx_ring[d->rx_tail].status & E1000_RXD_STAT_DD) {
        uint16_t plen = d->rx_ring[d->rx_tail].length;
        if (plen > 0u && plen <= (uint16_t)E1000_BUF_SZ) {
            spinlock_unlock(&d->lock);
            (void)fabric_netif_rx_submit(iface, d->rx_buf[d->rx_tail], plen);
            (void)net_ingress_frame(d->rx_buf[d->rx_tail], plen,
                                    (void*)iface);
            spinlock_lock(&d->lock);
        }

        /* Return descriptor to hardware */
        d->rx_ring[d->rx_tail].status = 0;
        e1000_wr(d, E1000_RDT, d->rx_tail);

        d->rx_tail = (d->rx_tail + 1u) % E1000_NUM_RX;
        processed++;
        if (processed >= E1000_NUM_RX) {
            break;   /* avoid spinning forever */
        }
    }

    spinlock_unlock(&d->lock);
    return RDNX_OK;
}

static void e1000_irq_handler(int vector, void* arg)
{
    e1000_dev_t* d = (e1000_dev_t*)arg;
    uint32_t icr;

    (void)vector;
    if (!d || !d->used) {
        return;
    }

    icr = e1000_rd(d, E1000_ICR);
    if (icr == 0u) {
        return;
    }

    if ((icr & (E1000_IC_RXT0 | E1000_IC_RXDMT0 | E1000_IC_LSC | E1000_IC_TXDW)) != 0u) {
        d->irq_pending = 1u;
    }
}

static void e1000_irq_disable(e1000_dev_t* d)
{
    if (!d) {
        return;
    }

    spinlock_lock(&d->lock);
    e1000_wr(d, E1000_IMC, 0xFFFFFFFFu);
    (void)e1000_rd(d, E1000_ICR);
    spinlock_unlock(&d->lock);

    pci_irq_unbind(d->fdev, e1000_irq_handler, &d->irq);
    memset(&d->irq, 0, sizeof(d->irq));
    d->irq.vector = -1;
}

static int e1000_irq_enable(e1000_dev_t* d)
{
    int rc;

    if (!d || !d->fdev) {
        return RDNX_E_INVALID;
    }

    memset(&d->irq, 0, sizeof(d->irq));
    d->irq.vector = -1;
    rc = pci_irq_bind(d->fdev, e1000_irq_handler, d, &d->irq);
    if (rc != RDNX_OK) {
        return rc;
    }
    if (d->irq.mode == PCI_IRQ_MODE_MSI) {
        kprintf("[E1000] MSI enabled on vector=%u\n", (unsigned)d->irq.vector);
    }

    spinlock_lock(&d->lock);
    (void)e1000_rd(d, E1000_ICR);
    e1000_wr(d, E1000_IMS, E1000_IC_RXT0 | E1000_IC_RXDMT0 | E1000_IC_LSC);
    spinlock_unlock(&d->lock);
    return RDNX_OK;
}

/* =========================================================================
 * Hardware initialisation
 * ======================================================================= */
static int e1000_hw_init(e1000_dev_t* d)
{
    /* Enable I/O space + bus mastering in PCI command */
    e1000_pci_cmd_set(d->pci_dev, d->pci_fn,
                      PCI_CMD_IO_EN | PCI_CMD_MEM_EN | PCI_CMD_BUS_MASTER);

    /* Software reset */
    e1000_wr(d, E1000_CTRL, E1000_CTRL_RST);
    /* Wait ~1 ms (spin ~500 k iterations) */
    for (volatile uint32_t i = 0; i < 500000u; i++) { }

    /* Mask all interrupts */
    e1000_wr(d, E1000_IMC, 0xFFFFFFFFu);

    /* Force link up */
    uint32_t ctrl = e1000_rd(d, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;
    ctrl &= ~E1000_CTRL_RST;
    e1000_wr(d, E1000_CTRL, ctrl);

    /* Clear MTA (multicast table) */
    for (uint32_t i = 0; i < 128u; i++) {
        e1000_wr(d, E1000_MTA + i * 4u, 0);
    }

    /* Read MAC from receive address registers */
    uint32_t ral = e1000_rd(d, E1000_RAL0);
    uint32_t rah = e1000_rd(d, E1000_RAH0);
    d->iface.mac[0] = (uint8_t)(ral        & 0xFFu);
    d->iface.mac[1] = (uint8_t)((ral >>  8) & 0xFFu);
    d->iface.mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
    d->iface.mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
    d->iface.mac[4] = (uint8_t)(rah        & 0xFFu);
    d->iface.mac[5] = (uint8_t)((rah >>  8) & 0xFFu);

    if (e1000_mac_is_zero(d->iface.mac)) {
        uint8_t fallback_mac[6] = {
            0x02, 0x52, 0x54, 0x00, 0x10, (uint8_t)(0x20u + d->index)
        };
        e1000_set_hw_mac(d, fallback_mac);
    }

    /* ---- Allocate and initialise RX ring ---- */
    d->rx_ring = (e1000_rx_desc_t*)e1000_alloc_dma(
        E1000_NUM_RX * sizeof(e1000_rx_desc_t), &d->rx_ring_phys);
    if (!d->rx_ring) {
        return RDNX_E_NOMEM;
    }

    for (uint32_t i = 0; i < E1000_NUM_RX; i++) {
        d->rx_buf[i] = (uint8_t*)e1000_alloc_dma(E1000_BUF_SZ, &d->rx_buf_phys[i]);
        if (!d->rx_buf[i]) {
            return RDNX_E_NOMEM;
        }
        d->rx_ring[i].addr   = d->rx_buf_phys[i];
        d->rx_ring[i].status = 0;
    }
    d->rx_tail = 0;

    e1000_wr(d, E1000_RDBAL, (uint32_t)(d->rx_ring_phys & 0xFFFFFFFFu));
    e1000_wr(d, E1000_RDBAH, (uint32_t)(d->rx_ring_phys >> 32));
    e1000_wr(d, E1000_RDLEN, E1000_NUM_RX * sizeof(e1000_rx_desc_t));
    e1000_wr(d, E1000_RDH,   0u);
    e1000_wr(d, E1000_RDT,   E1000_NUM_RX - 1u);  /* give N-1 slots to HW */

    /* ---- Allocate and initialise TX ring ---- */
    d->tx_ring = (e1000_tx_desc_t*)e1000_alloc_dma(
        E1000_NUM_TX * sizeof(e1000_tx_desc_t), &d->tx_ring_phys);
    if (!d->tx_ring) {
        return RDNX_E_NOMEM;
    }

    for (uint32_t i = 0; i < E1000_NUM_TX; i++) {
        d->tx_buf[i]      = (uint8_t*)e1000_alloc_dma(E1000_BUF_SZ, &d->tx_buf_phys[i]);
        if (!d->tx_buf[i]) {
            return RDNX_E_NOMEM;
        }
        d->tx_ring[i].addr = d->tx_buf_phys[i];
        d->tx_ring[i].sta = E1000_TXD_STAT_DD;
    }
    d->tx_tail = 0;

    e1000_wr(d, E1000_TDBAL, (uint32_t)(d->tx_ring_phys & 0xFFFFFFFFu));
    e1000_wr(d, E1000_TDBAH, (uint32_t)(d->tx_ring_phys >> 32));
    e1000_wr(d, E1000_TDLEN, E1000_NUM_TX * sizeof(e1000_tx_desc_t));
    e1000_wr(d, E1000_TDH,   0u);
    e1000_wr(d, E1000_TDT,   0u);

    /* ---- Enable transmitter ---- */
    e1000_wr(d, E1000_TCTL,
             E1000_TCTL_EN | E1000_TCTL_PSP |
             E1000_TCTL_CT | E1000_TCTL_COLD);
    e1000_wr(d, E1000_TIPG, E1000_TIPG_VALUE);

    /* ---- Enable receiver ---- */
    e1000_wr(d, E1000_RCTL,
             E1000_RCTL_EN  | E1000_RCTL_SBP  |
             E1000_RCTL_UPE | E1000_RCTL_MPE  |
             E1000_RCTL_BAM | E1000_RCTL_SECRC);

    return RDNX_OK;
}

/* =========================================================================
 * Fabric driver callbacks
 * ======================================================================= */

static bool e1000_probe(fabric_device_t* dev)
{
    if (!dev || dev->vendor_id != PCI_VENDOR_INTEL ||
        dev->class_code != PCI_CLASS_NETWORK) {
        return false;
    }
    for (uint32_t i = 0; i < E1000_DEVID_COUNT; i++) {
        if (dev->device_id == e1000_devids[i]) {
            return true;
        }
    }
    return false;
}

static int e1000_match_score(fabric_device_t* dev)
{
    if (!dev) {
        return FABRIC_MATCH_NONE;
    }
    if (e1000_probe(dev)) {
        return FABRIC_MATCH_DEVICE_EXACT;
    }
    if (dev->class_code == PCI_CLASS_NETWORK && dev->vendor_id == PCI_VENDOR_INTEL) {
        return FABRIC_MATCH_BUS_EXACT;
    }
    return FABRIC_MATCH_NONE;
}

static const fabric_property_t e1000_match_properties[] = {
    { .key = "bus", .type = FABRIC_PROP_STR, .value.str = "pci" },
    { .key = "vendor-id", .type = FABRIC_PROP_U32, .value.u32 = PCI_VENDOR_INTEL },
    { .key = "class-code", .type = FABRIC_PROP_U32, .value.u32 = PCI_CLASS_NETWORK },
};

static int e1000_attach(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }
    if (g_dev_count >= E1000_MAX_DEVS) {
        return RDNX_E_BUSY;
    }

    pci_device_info_t* pci = (pci_device_info_t*)dev->bus_private;
    if (!pci) {
        return RDNX_E_INVALID;
    }

    /* Find the I/O BAR (bit 0 = 1) */
    uint16_t io_base = 0;
    for (uint32_t b = 0; b < PCI_BAR_COUNT; b++) {
        uint32_t bar = pci->bars[b];
        if ((bar & 0x1u) == 0x1u) {   /* I/O space indicator */
            uint32_t base = bar & ~0x3u;
            if (base != 0u && base <= 0xFFFFu) {
                io_base = (uint16_t)base;
                break;
            }
        }
    }
    if (io_base == 0u) {
        kprintf("[E1000] no I/O BAR found (bars: %x %x %x %x %x %x)\n",
                pci->bars[0], pci->bars[1], pci->bars[2],
                pci->bars[3], pci->bars[4], pci->bars[5]);
    }

    if (fabric_net_service_init() != RDNX_OK) {
        kputs("[E1000] net service init failed\n");
        return RDNX_E_GENERIC;
    }

    e1000_dev_t* d = &g_devs[g_dev_count];
    memset(d, 0, sizeof(*d));
    d->used     = 1;
    d->index    = g_dev_count;
    d->io_base  = io_base;
    d->pci_dev  = pci->device;
    d->pci_fn   = pci->function;
    d->fdev     = dev;
    spinlock_init(&d->lock);

    if (e1000_map_mmio(d, pci->bars[0]) != RDNX_OK && io_base == 0u) {
        d->used = 0;
        return RDNX_E_UNSUPPORTED;
    }

    if (pci_has_capability(dev, PCIY_MSI)) {
        kprintf("[E1000] MSI capability present\n");
    }
    {
        int legacy_vector = pci_irq_vector(dev);
        if (legacy_vector >= 0) {
            kprintf("[E1000] legacy irq vector=%u line=%u pin=%u\n",
                    (unsigned)legacy_vector,
                    (unsigned)pci->interrupt_line,
                    (unsigned)pci->interrupt_pin);
        }
    }

    int rc = e1000_hw_init(d);
    if (rc != RDNX_OK) {
        kprintf("[E1000] hw init failed: %d\n", rc);
        d->used = 0;
        return rc;
    }

    /* Register Fabric network interface */
    static fabric_netif_ops_t ops = {
        .hdr  = RDNX_ABI_INIT(fabric_netif_ops_t),
        .tx   = e1000_tx_cb,
        .poll = e1000_poll_cb,
    };
    static const char* ifnames[E1000_MAX_DEVS] = { "eth0", "eth1" };

    d->iface.hdr          = RDNX_ABI_INIT(fabric_netif_t);
    d->iface.name         = ifnames[g_dev_count];
    d->iface.mtu          = 1500u;
    d->iface.flags        = FABRIC_NETIF_F_UP | FABRIC_NETIF_F_BROADCAST;
    /* QEMU usermode networking: guest IP = 10.0.2.15/24, gw = 10.0.2.2 */
    d->iface.ipv4_addr    = (10u  << 24) | (0u << 16) | (2u << 8) | 15u;
    d->iface.ipv4_netmask = 0xFFFFFF00u;
    d->iface.ipv4_gateway = (10u  << 24) | (0u << 16) | (2u << 8) | 2u;
    d->iface.ops          = &ops;
    d->iface.context      = d;

    rc = e1000_irq_enable(d);
    if (rc != RDNX_OK) {
        kprintf("[E1000] irq enable failed: %d (polling fallback)\n", rc);
    }

    if (fabric_netif_register(&d->iface) != RDNX_OK) {
        kputs("[E1000] netif register failed\n");
        e1000_irq_disable(d);
        d->used = 0;
        return RDNX_E_GENERIC;
    }

    kprintf("[E1000] %s ready — mac=%02x:%02x:%02x:%02x:%02x:%02x "
            "io=0x%x vendor=%04x device=%04x\n",
            d->iface.name,
            d->iface.mac[0], d->iface.mac[1], d->iface.mac[2],
            d->iface.mac[3], d->iface.mac[4], d->iface.mac[5],
            (unsigned)io_base,
            (unsigned)dev->vendor_id, (unsigned)dev->device_id);

    g_dev_count++;
    return RDNX_OK;
}

static int e1000_publish(fabric_device_t* dev)
{
    for (uint32_t i = 0; i < g_dev_count; i++) {
        if (g_devs[i].used && g_devs[i].fdev == dev) {
            return fabric_publish_netif_node(g_devs[i].iface.name, dev);
        }
    }
    return RDNX_E_NOTFOUND;
}

static void e1000_detach(fabric_device_t* dev)
{
    for (uint32_t i = 0; i < g_dev_count; i++) {
        if (g_devs[i].used && g_devs[i].fdev == dev) {
            e1000_irq_disable(&g_devs[i]);
            return;
        }
    }
}

static fabric_driver_t g_driver = {
    .name    = "e1000",
    .match_properties = e1000_match_properties,
    .match_property_count = 3,
    .match_priority = 25,
    .match_score = e1000_match_score,
    .probe   = e1000_probe,
    .attach  = e1000_attach,
    .publish = e1000_publish,
    .detach  = e1000_detach,
    .suspend = NULL,
    .resume  = NULL,
};

void e1000_net_stub_init(void)
{
    int rc = fabric_driver_register(&g_driver);
    if (rc == RDNX_OK) {
        kputs("[E1000] driver registered\n");
    } else {
        kprintf("[E1000] driver register failed: %d\n", rc);
    }
}
