/**
 * @file mb2_fb.c
 * @brief Multiboot2 linear framebuffer display driver
 *
 * Consumes the framebuffer descriptor passed by Limine (or any Multiboot2
 * bootloader) via tag type 8 and registers a gfx_display_t so that the
 * framebuffer console and userspace /dev/fb0 work on real hardware where
 * no Bochs/VBE PCI device is present.
 *
 * Virtual address layout
 * ----------------------
 * Maps the framebuffer at MB2_FB_VIRT_BASE (0xFFFFFFFFF6000000), which is
 * 64 MB below the Bochs VBE window (0xFFFFFFFFFA000000) and well clear of
 * every other fixed MMIO window.
 *
 * Ordering
 * --------
 * Must be called from sysinit_fabric() AFTER bochs_vbe_init() so that on
 * QEMU (which provides both MB2 tag and Bochs PCI device) the Bochs driver
 * wins as gfx0 (better resolution via VBE).  On real hardware bochs_vbe
 * finds no matching PCI device and this driver registers as gfx0.
 */

#include "../../../kernel/arch/x86_64/paging.h"
#include "../../../kernel/core/boot.h"
#include "../../../kernel/core/config.h"
#include "../../../include/gfx.h"
#include "../../../include/error.h"
#include "../../../include/console.h"
#include "../../../trace/bootlog.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * Virtual address window — 64 MB, below bochs-vbe window
 * ======================================================================= */
#define MB2_FB_VIRT_BASE  0xFFFFFFFFF6000000ULL
#define MB2_FB_SLOT_SIZE  0x4000000ULL   /* 64 MB */

/* =========================================================================
 * State
 * ======================================================================= */
static gfx_display_t g_mb2_disp;
static bool          g_registered = false;

/* =========================================================================
 * ops — no mode switching; the bootloader already set the mode
 * ======================================================================= */
static int mb2_fb_set_mode(gfx_display_t *disp, const gfx_mode_t *mode)
{
    /* Mode switching not supported for the boot framebuffer. */
    (void)disp;
    (void)mode;
    return RDNX_E_UNSUPPORTED;
}

static const gfx_display_ops_t g_mb2_fb_ops = {
    .set_mode  = mb2_fb_set_mode,
    .get_modes = NULL,
    .flush     = NULL,
};

/* =========================================================================
 * Framebuffer mapping
 * ======================================================================= */
static int mb2_fb_map(uint64_t phys, uint32_t size, void **virt_out)
{
    uint64_t virt  = MB2_FB_VIRT_BASE;
    uint64_t flags = PTE_PRESENT | PTE_RW | PTE_PCD;

    uint64_t aligned_size = ((uint64_t)size + PAGE_SIZE - 1u) & ~(uint64_t)(PAGE_SIZE - 1u);
    if (aligned_size > MB2_FB_SLOT_SIZE) {
        aligned_size = MB2_FB_SLOT_SIZE;
    }

    for (uint64_t off = 0u; off < aligned_size; off += PAGE_SIZE) {
        if (paging_map_page_4kb(virt + off, phys + off, flags) != 0) {
            return RDNX_E_GENERIC;
        }
    }

    *virt_out = (void *)(uintptr_t)virt;
    return RDNX_OK;
}

/* =========================================================================
 * Public init — called from sysinit_fabric()
 * ======================================================================= */
void mb2_fb_init(void)
{
    boot_info_t *bi = boot_get_info();
    if (!bi || !bi->fb_valid) {
        klog("display", "mb2_fb: no framebuffer tag from bootloader\n");
        return;
    }

    uint32_t fb_size = bi->fb_pitch * bi->fb_height;
    if (fb_size == 0u) {
        klog_warn("display", "mb2_fb: framebuffer size is zero\n");
        return;
    }

    void *virt = NULL;
    int rc = mb2_fb_map(bi->fb_phys, fb_size, &virt);
    if (rc != RDNX_OK) {
        klog_warn("display", "mb2_fb: failed to map framebuffer (rc=%d)\n", rc);
        return;
    }

    /* Zero the framebuffer to clear any bootloader remnants. */
    volatile uint32_t *pixels = (volatile uint32_t *)virt;
    uint32_t npixels = (bi->fb_pitch / 4u) * bi->fb_height;
    for (uint32_t i = 0u; i < npixels; i++) {
        pixels[i] = 0x00000000u;
    }

    g_mb2_disp.ops             = &g_mb2_fb_ops;
    g_mb2_disp.mode.width      = bi->fb_width;
    g_mb2_disp.mode.height     = bi->fb_height;
    g_mb2_disp.mode.pitch      = bi->fb_pitch;
    g_mb2_disp.mode.bpp        = bi->fb_bpp;
    g_mb2_disp.fb.phys_base    = (uintptr_t)bi->fb_phys;
    g_mb2_disp.fb.virt_base    = virt;
    g_mb2_disp.fb.size         = fb_size;
    g_mb2_disp.fb.mode         = g_mb2_disp.mode;
    g_mb2_disp.active          = true;
    g_mb2_disp.private         = NULL;

    rc = gfx_display_register(&g_mb2_disp);
    if (rc != RDNX_OK) {
        klog_warn("display", "mb2_fb: gfx_display_register failed: %d\n", rc);
        return;
    }

    g_registered = true;
    klog("display", "mb2_fb: %ux%u bpp=%u pitch=%u phys=0x%llx virt=%p\n",
         bi->fb_width, bi->fb_height, (unsigned)bi->fb_bpp, bi->fb_pitch,
         (unsigned long long)bi->fb_phys, virt);
}
