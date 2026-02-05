/**
 * @file vfs.h
 * @brief Minimal VFS interface (RAM-backed implementation)
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VFS_NODE_FILE = 0,
    VFS_NODE_DIR  = 1
} vfs_node_type_t;

typedef struct vfs_inode {
    vfs_node_type_t type;
    size_t size;
    size_t capacity;
    uint8_t* data;
} vfs_inode_t;

typedef struct vfs_node {
    char name[32];
    vfs_node_type_t type;
    struct vfs_node* parent;
    struct vfs_node* sibling;
    struct vfs_node* children;
    vfs_inode_t* inode;
} vfs_node_t;

typedef struct vfs_mount {
    const char* fs_name;
    vfs_node_t* root;
    vfs_node_t* mountpoint;
    struct vfs_mount* next;
} vfs_mount_t;

typedef struct vfs_file {
    vfs_node_t* node;
    size_t pos;
    bool writable;
} vfs_file_t;

typedef void (*vfs_list_cb_t)(const vfs_node_t* node, void* ctx);

enum {
    VFS_OPEN_READ   = 1 << 0,
    VFS_OPEN_WRITE  = 1 << 1,
    VFS_OPEN_CREATE = 1 << 2,
    VFS_OPEN_TRUNC  = 1 << 3
};

int vfs_init(void);
int vfs_is_ready(void);

void vfs_set_initrd(const void* data, size_t size);

int vfs_mount_ramfs(const char* path);
int vfs_mount_initrd_root(void);

int vfs_mkdir(const char* path);
int vfs_unlink(const char* path);
int vfs_list_dir(const char* path, vfs_list_cb_t cb, void* ctx);

int vfs_open(const char* path, int flags, vfs_file_t* out_file);
int vfs_close(vfs_file_t* file);
int vfs_read(vfs_file_t* file, void* buffer, size_t size);
int vfs_write(vfs_file_t* file, const void* buffer, size_t size);
