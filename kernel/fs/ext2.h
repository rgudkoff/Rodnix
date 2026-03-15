/**
 * @file ext2.h
 * @brief EXT2 filesystem driver bootstrap interface
 */

#pragma once

#include <stddef.h>
#include "vfs.h"

typedef struct ext2_fs_caps {
    int write_in_place;
    int write_extend;
    int truncate;
} ext2_fs_caps_t;

int ext2_fs_init(void);
int ext2_query_caps(ext2_fs_caps_t* out_caps);
int ext2_writeback_file(vfs_node_t* node, size_t off, const void* data, size_t len, size_t final_size);
int ext2_resize_file(vfs_node_t* node, size_t new_size);
int ext2_read_file_range(uint64_t ino_num, uint64_t offset, void* buf, size_t len);

struct vm_file_backing;
struct vm_file_backing* ext2_file_backing_create(vfs_inode_t* inode);
