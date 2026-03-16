/**
 * @file gfx.c
 * @brief GFX subsystem — display registry and software drawing helpers
 *
 * The subsystem keeps a flat array of up to GFX_DISPLAY_MAX registered
 * displays.  Drivers call gfx_display_register() from their Fabric
 * publish() callback after a successful set_mode().
 */

#include "../../include/gfx.h"
#include "../../include/error.h"
#include "../../include/console.h"
#include "../../fs/devfs.h"
#include "../../fs/vfs.h"
#include "../../mm/vm_map.h"
#include "../../trace/bootlog.h"
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Registry
 * ---------------------------------------------------------------------- */

static gfx_display_t *g_displays[GFX_DISPLAY_MAX];
static uint32_t       g_display_count;

static const char * const g_display_names[GFX_DISPLAY_MAX] = {
    "gfx0", "gfx1", "gfx2", "gfx3"
};

void gfx_init(void)
{
    for (uint32_t i = 0; i < GFX_DISPLAY_MAX; i++) {
        g_displays[i] = NULL;
    }
    g_display_count = 0;
    vm_set_fb_release_hook(gfx_fb_release_by_idx);
    klog("gfx", "subsystem ready\n");
}

int gfx_display_register(gfx_display_t *disp)
{
    if (!disp) {
        return RDNX_E_INVALID;
    }
    if (g_display_count >= GFX_DISPLAY_MAX) {
        return RDNX_E_BUSY;
    }
    disp->name = g_display_names[g_display_count];
    g_displays[g_display_count] = disp;
    g_display_count++;
    klog("gfx", "%s: %ux%u bpp=%u pitch=%u fb_phys=0x%llx\n",
         disp->name,
         disp->mode.width, disp->mode.height,
         (uint32_t)disp->mode.bpp,
         disp->mode.pitch,
         (unsigned long long)disp->fb.phys_base);

    disp->fb_owner_refcount = 0;
    disp->fb_owner_task_id  = 0;
    disp->on_fb_released    = NULL;

    /* Register /dev/fb0, /dev/fb1, … for userspace access via mmap(). */
    static const char * const fb_names[GFX_DISPLAY_MAX] = {
        "fb0", "fb1", "fb2", "fb3"
    };
    uint32_t idx = g_display_count - 1u;

    /* Derive pixel format from bpp (bochs VBE 32-bpp is always XRGB8888). */
    uint8_t pixel_fmt;
    if (disp->mode.bpp == 32u) {
        pixel_fmt = FB_PIXFMT_XRGB8888;
    } else if (disp->mode.bpp == 16u) {
        pixel_fmt = FB_PIXFMT_RGB565;
    } else {
        pixel_fmt = FB_PIXFMT_UNKNOWN;
    }

    devfs_register_framebuffer(fb_names[idx], idx,
                               (uint64_t)disp->fb.phys_base,
                               disp->mode.width, disp->mode.height,
                               disp->mode.pitch, disp->mode.bpp,
                               pixel_fmt,
                               (uint64_t)disp->fb.size);

    return RDNX_OK;
}

int gfx_fb_acquire(gfx_display_t *disp, uint64_t task_id)
{
    if (!disp) {
        return RDNX_E_INVALID;
    }
    if (disp->fb_owner_refcount > 0) {
        return RDNX_E_BUSY;
    }
    disp->fb_owner_refcount = 1;
    disp->fb_owner_task_id  = task_id;
    return RDNX_OK;
}

void gfx_fb_release(gfx_display_t *disp)
{
    if (!disp || disp->fb_owner_refcount == 0) {
        return;
    }
    disp->fb_owner_refcount--;
    if (disp->fb_owner_refcount == 0) {
        disp->fb_owner_task_id = 0;
        if (disp->on_fb_released) {
            disp->on_fb_released(disp);
        }
    }
}

int gfx_fb_release_for_task(gfx_display_t *disp, uint64_t task_id)
{
    if (!disp) {
        return RDNX_E_INVALID;
    }
    if (disp->fb_owner_refcount == 0) {
        return RDNX_E_INVALID;
    }
    if (disp->fb_owner_task_id != task_id) {
        return RDNX_E_DENIED;
    }
    gfx_fb_release(disp);
    return RDNX_OK;
}

void gfx_fb_release_by_idx(uint32_t display_idx)
{
    gfx_fb_release(gfx_display_get(display_idx));
}

void gfx_set_fb_release_cb(gfx_display_t *disp, void (*cb)(gfx_display_t *))
{
    if (disp) {
        disp->on_fb_released = cb;
    }
}

uint32_t gfx_display_count(void)
{
    return g_display_count;
}

gfx_display_t *gfx_display_get(uint32_t index)
{
    if (index >= g_display_count) {
        return NULL;
    }
    return g_displays[index];
}

gfx_display_t *gfx_display_primary(void)
{
    return (g_display_count > 0) ? g_displays[0] : NULL;
}

/* -------------------------------------------------------------------------
 * Software drawing helpers
 * ---------------------------------------------------------------------- */

void gfx_put_pixel(gfx_display_t *disp, uint32_t x, uint32_t y,
                   uint32_t color)
{
    if (!disp || !disp->fb.virt_base) {
        return;
    }
    if (x >= disp->mode.width || y >= disp->mode.height) {
        return;
    }
    uint32_t Bpp = (uint32_t)disp->mode.bpp / 8u;
    uint8_t *px  = (uint8_t *)disp->fb.virt_base
                   + (uint64_t)y * disp->mode.pitch
                   + (uint64_t)x * Bpp;
    switch (disp->mode.bpp) {
    case 32:
        *(volatile uint32_t *)px = color;
        break;
    case 24:
        px[0] = (uint8_t)(color);
        px[1] = (uint8_t)(color >> 8);
        px[2] = (uint8_t)(color >> 16);
        break;
    case 16:
        *(volatile uint16_t *)px = (uint16_t)color;
        break;
    default:
        break;
    }
}

void gfx_fill_rect(gfx_display_t *disp,
                   uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   uint32_t color)
{
    if (!disp || !disp->fb.virt_base || w == 0u || h == 0u) {
        return;
    }
    if (x >= disp->mode.width || y >= disp->mode.height) {
        return;
    }
    if (x + w > disp->mode.width)  { w = disp->mode.width  - x; }
    if (y + h > disp->mode.height) { h = disp->mode.height - y; }

    uint32_t Bpp   = (uint32_t)disp->mode.bpp / 8u;
    uint32_t pitch = disp->mode.pitch;
    uint8_t *fb    = (uint8_t *)disp->fb.virt_base;

    /* Pre-compute packed pixel value for 32-bpp fast path */
    for (uint32_t row = 0u; row < h; row++) {
        uint8_t *line = fb + (uint64_t)(y + row) * pitch + (uint64_t)x * Bpp;
        switch (disp->mode.bpp) {
        case 32:
            for (uint32_t col = 0u; col < w; col++) {
                *(volatile uint32_t *)line = color;
                line += 4u;
            }
            break;
        case 24:
            for (uint32_t col = 0u; col < w; col++) {
                line[0] = (uint8_t)(color);
                line[1] = (uint8_t)(color >> 8);
                line[2] = (uint8_t)(color >> 16);
                line += 3u;
            }
            break;
        case 16:
            for (uint32_t col = 0u; col < w; col++) {
                *(volatile uint16_t *)line = (uint16_t)color;
                line += 2u;
            }
            break;
        default:
            break;
        }
    }
}

void gfx_blit(gfx_display_t *disp,
              uint32_t dst_x, uint32_t dst_y,
              const void *src, uint32_t src_pitch,
              uint32_t w, uint32_t h,
              uint8_t bpp)
{
    if (!disp || !disp->fb.virt_base || !src || w == 0u || h == 0u) {
        return;
    }
    if (dst_x >= disp->mode.width || dst_y >= disp->mode.height) {
        return;
    }
    if (dst_x + w > disp->mode.width)  { w = disp->mode.width  - dst_x; }
    if (dst_y + h > disp->mode.height) { h = disp->mode.height - dst_y; }

    uint32_t dst_Bpp = (uint32_t)disp->mode.bpp / 8u;
    uint32_t src_Bpp = (uint32_t)bpp / 8u;
    uint32_t copy_w  = w * (dst_Bpp < src_Bpp ? dst_Bpp : src_Bpp);
    uint8_t *fb      = (uint8_t *)disp->fb.virt_base;

    for (uint32_t row = 0u; row < h; row++) {
        uint8_t       *dst_line = fb
                                  + (uint64_t)(dst_y + row) * disp->mode.pitch
                                  + (uint64_t)dst_x * dst_Bpp;
        const uint8_t *src_line = (const uint8_t *)src + (uint64_t)row * src_pitch;
        for (uint32_t b = 0u; b < copy_w; b++) {
            dst_line[b] = src_line[b];
        }
    }
}
