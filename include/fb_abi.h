/**
 * @file fb_abi.h
 * @brief Framebuffer device userspace ABI — /dev/fb*
 *
 * Stable interface between userspace and kernel framebuffer device nodes.
 * See docs/ru/framebuffer_ownership_spec.md for the full ownership contract.
 */

#pragma once

#include <stdint.h>

/* -------------------------------------------------------------------------
 * ioctl request codes
 * ---------------------------------------------------------------------- */

/** Attempt to become the active onscreen owner.
 *  Returns 0 on success, EBUSY if another owner already holds the display. */
#define FB_IOC_ACQUIRE      0xFB01u

/** Release onscreen ownership held by the calling process.
 *  Returns EPERM if the caller is not the current owner. */
#define FB_IOC_RELEASE      0xFB02u

/** Query the current ownership state.
 *  Argument: pointer to fb_owner_info_t, filled on success. */
#define FB_IOC_QUERY_OWNER  0xFB03u

/* -------------------------------------------------------------------------
 * Ownership states (fb_owner_info_t.state)
 * ---------------------------------------------------------------------- */
#define FB_OWNER_TEXT_CONSOLE       0u  /* kernel text console owns the screen */
#define FB_OWNER_FRAMEBUFFER_CLIENT 1u  /* userspace process owns the screen   */

/* -------------------------------------------------------------------------
 * FB_IOC_QUERY_OWNER result
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t state;         /* FB_OWNER_TEXT_CONSOLE or FB_OWNER_FRAMEBUFFER_CLIENT */
    uint32_t _pad;
    uint64_t owner_task_id; /* task_id of current owner; 0 if TEXT_CONSOLE          */
} fb_owner_info_t;
