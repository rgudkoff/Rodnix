/**
 * @file bochs_vbe.c
 * @brief Bochs / QEMU display adapter driver (GFX subsystem)
 *
 * Targets the "Bochs Display Adapter" exposed by QEMU (-vga std / -vga virtio
 * with legacy emulation, or -device VGA):
 *   PCI vendor 0x1234, device 0x1111
 *
 * Hardware interface
 * ------------------
 * - BAR0: linear framebuffer (memory BAR, 32-bit or 64-bit)
 * - Bochs VBE index/data I/O ports at 0x01CE / 0x01CF
 *   (also mirrored at BAR2 + 0x500 as MMIO, but I/O is simpler)
 *
 * Driver initialises the adapter to 1024×768 at 32 bpp, maps the
 * framebuffer into kernel virtual address space, and registers a
 * gfx_display_t with the GFX subsystem.  It also publishes Fabric
 * service nodes "gfx0" / "fb0" for higher-level consumers.
 *
 * Locking
 * -------
 * Mode-set and framebuffer access do not race during early init; no
 * spinlock is needed here.  A per-device lock should be added once
 * concurrent mode-switching is required.
 */

#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/fabric/bus/pci.h"
#include "../../../kernel/arch/x86_64/paging.h"
#include "../../../kernel/arch/x86_64/config.h"
#include "../../../kernel/core/memory.h"
#include "../../../include/gfx.h"
#include "../../../include/error.h"
#include "../../../include/console.h"
#include "../../../trace/bootlog.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * PCI identity
 * ======================================================================= */
#define BOCHS_VBE_VENDOR  0x1234u
#define BOCHS_VBE_DEVICE  0x1111u

#define PCI_CLASS_DISPLAY 0x03u
#define PCI_SUBCLASS_VGA  0x00u

/* PCI command register bits */
#define PCI_CMD_IO_EN      (1u << 0)
#define PCI_CMD_MEM_EN     (1u << 1)
#define PCI_CMD_BUS_MASTER (1u << 2)

/* =========================================================================
 * Bochs VBE I/O ports and register indices
 * ======================================================================= */
#define VBE_DISPI_IOPORT_INDEX  0x01CEu
#define VBE_DISPI_IOPORT_DATA   0x01CFu

#define VBE_DISPI_INDEX_ID           0x0u
#define VBE_DISPI_INDEX_XRES         0x1u
#define VBE_DISPI_INDEX_YRES         0x2u
#define VBE_DISPI_INDEX_BPP          0x3u
#define VBE_DISPI_INDEX_ENABLE       0x4u
#define VBE_DISPI_INDEX_BANK         0x5u
#define VBE_DISPI_INDEX_VIRT_WIDTH   0x6u
#define VBE_DISPI_INDEX_VIRT_HEIGHT  0x7u
#define VBE_DISPI_INDEX_X_OFFSET     0x8u
#define VBE_DISPI_INDEX_Y_OFFSET     0x9u

#define VBE_DISPI_DISABLED    0x00u
#define VBE_DISPI_ENABLED     0x01u
#define VBE_DISPI_LFB_ENABLED 0x40u   /* use linear (non-banked) framebuffer */
#define VBE_DISPI_NOCLEARMEM  0x80u   /* don't clear VRAM on mode set       */

#define VBE_DISPI_ID4         0xB0C4u  /* minimum supported version          */

/* =========================================================================
 * Framebuffer virtual address layout
 *
 * Reserve 64 MB per display starting at BOCHS_FB_VIRT_BASE.
 * This window sits below the e1000 MMIO window (0xFFFFFFFFFEC00000).
 * ======================================================================= */
#define BOCHS_FB_VIRT_BASE  0xFFFFFFFFFA000000ULL
#define BOCHS_FB_SLOT_SIZE  0x4000000ULL   /* 64 MB per display */

/* Default mode programmed at attach time */
#define BOCHS_DEFAULT_WIDTH  1024u
#define BOCHS_DEFAULT_HEIGHT 768u
#define BOCHS_DEFAULT_BPP    32u

#define BOCHS_SLOT_MAX 2u

/* =========================================================================
 * Per-device state
 * ======================================================================= */
typedef struct {
    int            used;
    uint32_t       index;       /* slot index: 0, 1, … */
    uint64_t       fb_phys;     /* BAR0 physical address */
    void          *fb_virt;     /* kernel-virtual mapping */
    uint32_t       fb_size;     /* mapped size in bytes   */
    fabric_device_t *fdev;
    gfx_display_t  gfx;        /* embedded gfx handle    */
} bochs_dev_t;

static bochs_dev_t g_slots[BOCHS_SLOT_MAX];

/* =========================================================================
 * I/O helpers
 * ======================================================================= */
static inline void vbe_out16(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %%ax, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t vbe_in16(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %%ax" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void vbe_write(uint16_t index, uint16_t val)
{
    vbe_out16(VBE_DISPI_IOPORT_INDEX, index);
    vbe_out16(VBE_DISPI_IOPORT_DATA,  val);
}

static inline uint16_t vbe_read(uint16_t index)
{
    vbe_out16(VBE_DISPI_IOPORT_INDEX, index);
    return vbe_in16(VBE_DISPI_IOPORT_DATA);
}

/* =========================================================================
 * PCI config-space helpers (mirrored from pci.c internals)
 * ======================================================================= */
static inline uint32_t bochs_pci_cfg_addr(uint8_t bus, uint8_t dev,
                                           uint8_t fn,  uint8_t off)
{
    return 0x80000000u
         | ((uint32_t)bus << 16)
         | ((uint32_t)dev << 11)
         | ((uint32_t)fn  <<  8)
         | (off & 0xFCu);
}

static inline uint32_t bochs_pci_read32(uint8_t bus, uint8_t dev,
                                         uint8_t fn,  uint8_t off)
{
    uint32_t addr = bochs_pci_cfg_addr(bus, dev, fn, off);
    uint32_t val;
    __asm__ volatile ("outl %%eax, %1" : : "a"(addr), "Nd"((uint16_t)0xCF8u));
    __asm__ volatile ("inl  %1, %%eax" : "=a"(val)   : "Nd"((uint16_t)0xCFCu));
    return val;
}

static inline void bochs_pci_write16(uint8_t bus, uint8_t dev,
                                      uint8_t fn,  uint8_t off,
                                      uint16_t val)
{
    uint32_t addr = bochs_pci_cfg_addr(bus, dev, fn, off);
    __asm__ volatile ("outl %%eax, %1" : : "a"(addr), "Nd"((uint16_t)0xCF8u));
    __asm__ volatile ("outw %%ax,  %1" : : "a"(val),  "Nd"((uint16_t)(0xCFCu + (off & 2u))));
}

/* =========================================================================
 * Framebuffer mapping
 * ======================================================================= */
static int bochs_map_fb(bochs_dev_t *d, uint64_t phys, uint32_t size)
{
    uint64_t virt  = BOCHS_FB_VIRT_BASE + (uint64_t)d->index * BOCHS_FB_SLOT_SIZE;
    uint64_t flags = PTE_PRESENT | PTE_RW | PTE_PCD;  /* uncached write-combining */

    uint64_t aligned_size = (size + PAGE_SIZE - 1u) & ~(uint64_t)(PAGE_SIZE - 1u);
    if (aligned_size > BOCHS_FB_SLOT_SIZE) {
        aligned_size = BOCHS_FB_SLOT_SIZE;
    }

    for (uint64_t off = 0u; off < aligned_size; off += PAGE_SIZE) {
        if (paging_map_page_4kb(virt + off, phys + off, flags) != 0) {
            return RDNX_E_GENERIC;
        }
    }
    d->fb_phys = phys;
    d->fb_virt = (void *)(uintptr_t)virt;
    d->fb_size = (uint32_t)aligned_size;
    return RDNX_OK;
}

/* =========================================================================
 * Bochs VBE mode set
 * ======================================================================= */
static int bochs_set_mode_hw(uint32_t w, uint32_t h, uint8_t bpp)
{
    /* Disable display while reconfiguring */
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_INDEX_XRES,   (uint16_t)w);
    vbe_write(VBE_DISPI_INDEX_YRES,   (uint16_t)h);
    vbe_write(VBE_DISPI_INDEX_BPP,    (uint16_t)bpp);
    /* Virtual framebuffer == physical scanout area */
    vbe_write(VBE_DISPI_INDEX_VIRT_WIDTH,  (uint16_t)w);
    vbe_write(VBE_DISPI_INDEX_VIRT_HEIGHT, (uint16_t)h);
    vbe_write(VBE_DISPI_INDEX_X_OFFSET, 0u);
    vbe_write(VBE_DISPI_INDEX_Y_OFFSET, 0u);
    /* Enable: linear framebuffer; allow VRAM clear to eliminate VGA artifacts */
    vbe_write(VBE_DISPI_INDEX_ENABLE,
              VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    return RDNX_OK;
}

/* =========================================================================
 * gfx_display_ops implementation
 * ======================================================================= */
static int bochs_gfx_set_mode(gfx_display_t *disp, const gfx_mode_t *mode)
{
    if (!disp || !mode) {
        return RDNX_E_INVALID;
    }
    bochs_dev_t *d = (bochs_dev_t *)disp->private;
    if (!d) {
        return RDNX_E_INVALID;
    }

    int rc = bochs_set_mode_hw(mode->width, mode->height, mode->bpp);
    if (rc != RDNX_OK) {
        return rc;
    }

    uint32_t pitch = mode->width * ((uint32_t)mode->bpp / 8u);
    uint32_t fb_sz = pitch * mode->height;

    /* Remap framebuffer if size changed */
    if (fb_sz > d->fb_size || !d->fb_virt) {
        rc = bochs_map_fb(d, d->fb_phys, fb_sz);
        if (rc != RDNX_OK) {
            return rc;
        }
    }

    disp->mode.width  = mode->width;
    disp->mode.height = mode->height;
    disp->mode.bpp    = mode->bpp;
    disp->mode.pitch  = pitch;

    disp->fb.phys_base = d->fb_phys;
    disp->fb.virt_base = d->fb_virt;
    disp->fb.size      = d->fb_size;
    disp->fb.mode      = disp->mode;

    disp->active = true;
    return RDNX_OK;
}

static const gfx_display_ops_t g_bochs_gfx_ops = {
    .set_mode  = bochs_gfx_set_mode,
    .get_modes = NULL,
    .flush     = NULL,
};

/* =========================================================================
 * Fabric driver callbacks
 * ======================================================================= */
static bool bochs_vbe_probe(fabric_device_t *dev)
{
    if (!dev) {
        return false;
    }
    return (dev->vendor_id == BOCHS_VBE_VENDOR &&
            dev->device_id == BOCHS_VBE_DEVICE);
}

static int bochs_vbe_match_score(fabric_device_t *dev)
{
    if (!dev) {
        return FABRIC_MATCH_NONE;
    }
    if (dev->vendor_id == BOCHS_VBE_VENDOR &&
        dev->device_id == BOCHS_VBE_DEVICE) {
        return FABRIC_MATCH_DEVICE_EXACT;
    }
    return FABRIC_MATCH_NONE;
}

static int bochs_vbe_attach(fabric_device_t *dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }

    /* Find a free slot */
    bochs_dev_t *d = NULL;
    for (uint32_t i = 0u; i < BOCHS_SLOT_MAX; i++) {
        if (!g_slots[i].used) {
            d = &g_slots[i];
            d->index = i;
            break;
        }
    }
    if (!d) {
        return RDNX_E_BUSY;
    }

    pci_device_info_t *pci = (pci_device_info_t *)dev->bus_private;
    if (!pci) {
        return RDNX_E_INVALID;
    }

    /* Verify Bochs VBE is present */
    uint16_t vbe_id = vbe_read(VBE_DISPI_INDEX_ID);
    if (vbe_id < VBE_DISPI_ID4) {
        kprintf("[BOCHS_VBE] VBE ID 0x%x too old, expected >= 0x%x\n",
                (uint32_t)vbe_id, (uint32_t)VBE_DISPI_ID4);
        return RDNX_E_UNSUPPORTED;
    }

    /* Extract framebuffer physical address from BAR0 */
    uint32_t bar0 = pci->bars[0];
    if ((bar0 & 0x1u) != 0u) {
        /* BAR0 is an I/O BAR — unexpected */
        kprintf("[BOCHS_VBE] BAR0 is I/O, expected memory BAR\n");
        return RDNX_E_UNSUPPORTED;
    }
    uint64_t fb_phys = (uint64_t)(bar0 & ~0xFu);
    if ((bar0 & 0x6u) == 0x4u) {
        /* 64-bit BAR: upper 32 bits in BAR1 */
        fb_phys |= (uint64_t)pci->bars[1] << 32;
    }
    if (fb_phys == 0u) {
        kprintf("[BOCHS_VBE] BAR0 physical address is zero\n");
        return RDNX_E_GENERIC;
    }

    /* Enable PCI memory decode + bus master */
    bochs_pci_write16(pci->bus, pci->device, pci->function, 0x04,
                      (uint16_t)(PCI_CMD_MEM_EN | PCI_CMD_BUS_MASTER));

    /* Program default mode */
    int rc = bochs_set_mode_hw(BOCHS_DEFAULT_WIDTH,
                                BOCHS_DEFAULT_HEIGHT,
                                BOCHS_DEFAULT_BPP);
    if (rc != RDNX_OK) {
        return rc;
    }

    uint32_t pitch  = BOCHS_DEFAULT_WIDTH * (BOCHS_DEFAULT_BPP / 8u);
    uint32_t fb_sz  = pitch * BOCHS_DEFAULT_HEIGHT;

    /* Map framebuffer */
    rc = bochs_map_fb(d, fb_phys, fb_sz);
    if (rc != RDNX_OK) {
        kprintf("[BOCHS_VBE] failed to map framebuffer\n");
        return rc;
    }

    /* Fill in gfx_display_t */
    d->gfx.ops             = &g_bochs_gfx_ops;
    d->gfx.mode.width      = BOCHS_DEFAULT_WIDTH;
    d->gfx.mode.height     = BOCHS_DEFAULT_HEIGHT;
    d->gfx.mode.bpp        = BOCHS_DEFAULT_BPP;
    d->gfx.mode.pitch      = pitch;
    d->gfx.fb.phys_base    = fb_phys;
    d->gfx.fb.virt_base    = d->fb_virt;
    d->gfx.fb.size         = d->fb_size;
    d->gfx.fb.mode         = d->gfx.mode;
    d->gfx.active          = true;
    d->gfx.private         = d;

    /* Fill background to confirm framebuffer mapping is live */
    volatile uint32_t *pixels = (volatile uint32_t *)d->fb_virt;
    uint32_t npixels = (pitch / 4u) * BOCHS_DEFAULT_HEIGHT;
    for (uint32_t i = 0u; i < npixels; i++) {
        pixels[i] = 0x00000000u; /* black */
    }

    d->fdev = dev;
    d->used = 1;
    dev->driver_state = d;

    /* Register with the GFX subsystem now — fabric_finalize_driver_attach
     * skips publish() when driver_state is already set, so we do it here. */
    int gfx_rc = gfx_display_register(&d->gfx);
    if (gfx_rc != RDNX_OK) {
        kprintf("[BOCHS_VBE] gfx_display_register failed: %d\n", gfx_rc);
        /* Non-fatal: continue without GFX subsystem registration */
    }

    klog("display", "bochs-vbe slot %u attached: %ux%u bpp=%u fb_phys=0x%llx virt=%p\n",
         d->index,
         BOCHS_DEFAULT_WIDTH, BOCHS_DEFAULT_HEIGHT, BOCHS_DEFAULT_BPP,
         (unsigned long long)fb_phys, d->fb_virt);
    return RDNX_OK;
}

static int bochs_vbe_publish(fabric_device_t *dev)
{
    if (!dev || !dev->driver_state) {
        return RDNX_E_INVALID;
    }
    bochs_dev_t *d = (bochs_dev_t *)dev->driver_state;

    /* gfx_display_register was already called from bochs_vbe_attach to work
     * around the Fabric finalize early-return.  Skip it here to avoid a
     * double-registration. */

    /* Publish Fabric service nodes */
    fabric_publish_service_node(d->gfx.name, "display", dev);

    /* Also publish a legacy "fb0"/"fb1" alias for compatibility */
    static const char * const fb_names[] = { "fb0", "fb1" };
    if (d->index < 2u) {
        fabric_publish_service_node(fb_names[d->index], "display", dev);
    }

    return RDNX_OK;
}

static void bochs_vbe_detach(fabric_device_t *dev)
{
    if (!dev || !dev->driver_state) {
        return;
    }
    bochs_dev_t *d = (bochs_dev_t *)dev->driver_state;
    /* Disable display */
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    d->used = 0;
    d->fb_virt = NULL;
    dev->driver_state = NULL;
}

/* =========================================================================
 * Match properties — Bochs VBE PCI identity
 * ======================================================================= */
static const fabric_property_t g_bochs_match_props[] = {
    { .key = "bus",       .type = FABRIC_PROP_STR, .value.str = "pci"           },
    { .key = "vendor-id", .type = FABRIC_PROP_U32, .value.u32 = BOCHS_VBE_VENDOR },
    { .key = "device-id", .type = FABRIC_PROP_U32, .value.u32 = BOCHS_VBE_DEVICE },
};

static fabric_driver_t g_bochs_driver = {
    .name                = "bochs-vbe",
    .match_properties    = g_bochs_match_props,
    .match_property_count = 3u,
    .match_priority      = 0,
    .match_score         = bochs_vbe_match_score,
    .probe               = bochs_vbe_probe,
    .attach              = bochs_vbe_attach,
    .publish             = bochs_vbe_publish,
    .detach              = bochs_vbe_detach,
    .suspend             = NULL,
    .resume              = NULL,
};

/* =========================================================================
 * Module init
 * ======================================================================= */
void bochs_vbe_init(void)
{
    for (uint32_t i = 0u; i < BOCHS_SLOT_MAX; i++) {
        g_slots[i].used = 0;
    }
    int rc = fabric_driver_register(&g_bochs_driver);
    if (rc == RDNX_OK) {
        klog("display", "bochs-vbe driver registered\n");
    } else {
        klog_warn("display", "bochs-vbe driver registration failed: %d\n", rc);
    }
}
