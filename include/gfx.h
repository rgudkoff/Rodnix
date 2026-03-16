/**
 * @file gfx.h
 * @brief GFX subsystem — kernel graphics device interface
 *
 * Provides a thin abstraction layer over framebuffer-capable display
 * adapters.  Drivers allocate a gfx_display_t, fill in the ops vtable,
 * then call gfx_display_register().  Consumers (console, compositor,
 * userland DRM proxy) obtain a display via gfx_display_get() and call
 * ops->set_mode() to switch resolutions.
 */

#ifndef _RODNIX_GFX_H
#define _RODNIX_GFX_H

#include <stdint.h>
#include <stdbool.h>

#define GFX_DISPLAY_MAX 4u

/* -------------------------------------------------------------------------
 * Display mode descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t width;   /* horizontal resolution in pixels */
    uint32_t height;  /* vertical resolution in pixels   */
    uint32_t pitch;   /* bytes per scanline              */
    uint8_t  bpp;     /* bits per pixel: 16, 24, or 32   */
    uint8_t  _pad[3];
} gfx_mode_t;

/* -------------------------------------------------------------------------
 * Framebuffer descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    uintptr_t phys_base;  /* physical address of the linear framebuffer */
    void     *virt_base;  /* kernel-mapped virtual address (NULL if unmapped) */
    uint32_t  size;       /* total framebuffer size in bytes */
    uint8_t   _pad[4];
    gfx_mode_t mode;      /* mode in effect when this fb was mapped */
} gfx_fb_t;

/* -------------------------------------------------------------------------
 * Driver ops vtable
 * ---------------------------------------------------------------------- */
typedef struct gfx_display gfx_display_t;

typedef struct {
    /**
     * Set display to the requested mode.  The driver maps (or remaps) the
     * framebuffer, updates disp->fb and disp->mode, then returns RDNX_OK.
     */
    int  (*set_mode)(gfx_display_t *disp, const gfx_mode_t *mode);

    /**
     * Enumerate hardware-supported modes into @modes[0..max-1].
     * Writes the count actually stored to *count.  Optional — may be NULL.
     */
    int  (*get_modes)(gfx_display_t *disp, gfx_mode_t *modes,
                      uint32_t max, uint32_t *count);

    /**
     * Notify the driver that pixels in the given rectangle have been
     * updated and need to be flushed to the display (e.g., for banked
     * or double-buffered adapters).  Optional — may be NULL.
     */
    void (*flush)(gfx_display_t *disp,
                  uint32_t x, uint32_t y, uint32_t w, uint32_t h);
} gfx_display_ops_t;

/* -------------------------------------------------------------------------
 * Display device handle (allocated and owned by the driver)
 * ---------------------------------------------------------------------- */
struct gfx_display {
    const char              *name;           /* assigned by gfx_display_register: "gfx0" … */
    const gfx_display_ops_t *ops;            /* driver vtable                               */
    gfx_fb_t                 fb;             /* current framebuffer mapping                  */
    gfx_mode_t               mode;           /* current active mode                          */
    bool                     active;         /* set to true after successful set_mode        */
    bool                     _pad_active[3];
    uint32_t                 fb_owner_refcount; /* >0 while a userspace client owns the display */
    uint32_t                 _pad_refcount;
    uint64_t                 fb_owner_task_id;  /* task_id of current owner; 0 = none           */
    /* Called when fb_owner_refcount drops to 0 (userspace released the display). */
    void                   (*on_fb_released)(gfx_display_t *disp);
    void                    *private;        /* driver private state                         */
};

/* -------------------------------------------------------------------------
 * Subsystem API
 * ---------------------------------------------------------------------- */

/** Initialise the GFX subsystem (must be called before any registration). */
void gfx_init(void);

/** Register a display device.  Assigns disp->name and adds to the registry. */
int  gfx_display_register(gfx_display_t *disp);

/** Return number of registered displays. */
uint32_t       gfx_display_count(void);

/** Return display by index, or NULL if out of range. */
gfx_display_t *gfx_display_get(uint32_t index);

/** Return the primary (index 0) display, or NULL. */
gfx_display_t *gfx_display_primary(void);

/** Attempt to acquire display ownership for a task.
 *  task_id identifies the owning process (use 0 for anonymous/mmap autograb).
 *  Returns RDNX_OK on success, RDNX_E_BUSY if another owner already holds it. */
int gfx_fb_acquire(gfx_display_t *disp, uint64_t task_id);

/** Unconditional release — decrement refcount and fire callback when it hits 0. */
void gfx_fb_release(gfx_display_t *disp);

/** Release ownership only if the caller's task_id matches the stored owner.
 *  Returns RDNX_E_DENIED if task_id does not match. */
int gfx_fb_release_for_task(gfx_display_t *disp, uint64_t task_id);

/** gfx_fb_release looked up by display index — used as vm_set_fb_release_hook target. */
void gfx_fb_release_by_idx(uint32_t display_idx);

/** Register a callback invoked when the last userspace owner releases the display. */
void gfx_set_fb_release_cb(gfx_display_t *disp, void (*cb)(gfx_display_t *));

/* -------------------------------------------------------------------------
 * Software drawing helpers (operate on disp->fb.virt_base)
 * ---------------------------------------------------------------------- */

/** Write a single pixel.  color is 0xAARRGGBB for 32-bpp. */
void gfx_put_pixel(gfx_display_t *disp, uint32_t x, uint32_t y,
                   uint32_t color);

/** Fill a rectangle with a solid color. */
void gfx_fill_rect(gfx_display_t *disp,
                   uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   uint32_t color);

/**
 * Blit a pixel buffer into the framebuffer.
 * @src_pitch  stride of the source buffer in bytes
 * @bpp        bits per pixel of the source buffer
 */
void gfx_blit(gfx_display_t *disp,
              uint32_t dst_x, uint32_t dst_y,
              const void *src, uint32_t src_pitch,
              uint32_t w, uint32_t h,
              uint8_t bpp);

#endif /* _RODNIX_GFX_H */
