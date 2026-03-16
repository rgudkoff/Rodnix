/**
 * @file gfx_console.h
 * @brief Framebuffer text console
 *
 * Renders console output onto a GFX framebuffer using an embedded 8x8
 * IBM bitmap font (each glyph is drawn as 8×16 pixels by doubling rows).
 *
 * Usage:
 *   gfx_console_attach(gfx_display_primary());
 *   console_set_gfx_putc(gfx_console_putc);
 */

#ifndef _RODNIX_GFX_CONSOLE_H
#define _RODNIX_GFX_CONSOLE_H

#include "gfx.h"
#include <stdint.h>
#include <stdbool.h>

/** Attach the GFX console to a display.  Clears the framebuffer. */
void gfx_console_attach(gfx_display_t *disp);

/** Returns true if a display has been attached. */
bool gfx_console_active(void);

/** Render one character (handles \n, \r, \b, \t, scrolling). */
void gfx_console_putc(char c);

/** Clear the screen and home the cursor. */
void gfx_console_clear(void);

/** Move cursor to (col, row) (0-based). */
void gfx_console_set_cursor(uint32_t col, uint32_t row);

#endif /* _RODNIX_GFX_CONSOLE_H */
