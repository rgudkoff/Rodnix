/**
 * @file devfs.h
 * @brief Device filesystem driver bootstrap interface
 */

#pragma once

#include <stdint.h>

int devfs_fs_init(void);
int devfs_register_blockdev(const char* name);
int devfs_register_framebuffer(const char* name,
                               uint32_t display_idx,
                               uint64_t phys_base,
                               uint32_t width, uint32_t height,
                               uint32_t pitch, uint8_t bpp,
                               uint8_t pixel_format,
                               uint64_t size);
