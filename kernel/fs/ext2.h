/**
 * @file ext2.h
 * @brief EXT2 filesystem driver bootstrap interface
 */

#pragma once

#include <stddef.h>
#include "vfs.h"

int ext2_fs_init(void);
int ext2_writeback_file(vfs_node_t* node, size_t off, const void* data, size_t len, size_t final_size);
