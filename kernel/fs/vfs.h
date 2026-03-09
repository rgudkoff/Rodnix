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
    uint32_t flags;
    uint32_t fs_tag;
    uint32_t fs_aux;
    uint64_t fs_ino;
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

typedef struct vfs_stat {
    uint32_t mode;
    uint64_t size;
} vfs_stat_t;

typedef void (*vfs_list_cb_t)(const vfs_node_t* node, void* ctx);
typedef int (*vfs_mount_fn_t)(const char* source, vfs_node_t** out_root);

typedef struct vfs_fs_driver {
    const char* name;
    vfs_mount_fn_t mount;
} vfs_fs_driver_t;

enum {
    VFS_OPEN_READ   = 1 << 0,
    VFS_OPEN_WRITE  = 1 << 1,
    VFS_OPEN_CREATE = 1 << 2,
    VFS_OPEN_TRUNC  = 1 << 3
};

enum {
    VFS_INODE_CONSOLE = 1u << 0,
    VFS_INODE_DEV_NULL = 1u << 1,
    VFS_INODE_DEV_ZERO = 1u << 2,
    VFS_INODE_CHARDEV = 1u << 3,
    VFS_INODE_BLOCKDEV = 1u << 4
};

enum {
    VFS_FS_TAG_NONE = 0u,
    VFS_FS_TAG_EXT2 = 1u
};

int vfs_init(void);
int vfs_is_ready(void);

void vfs_set_initrd(const void* data, size_t size);

int vfs_register_fs(const vfs_fs_driver_t* driver);
int vfs_mount(const char* fs_name, const char* source, const char* target);
int vfs_mount_ramfs(const char* path);
int vfs_mount_initrd_root(void);

vfs_node_t* vfs_fs_alloc_node(const char* name, vfs_node_type_t type);
int vfs_fs_add_child(vfs_node_t* parent, vfs_node_t* child);
int vfs_fs_set_file_data(vfs_node_t* node, const void* data, size_t size);

int vfs_mkdir(const char* path);
int vfs_unlink(const char* path);
int vfs_rename(const char* old_path, const char* new_path);
int vfs_list_dir(const char* path, vfs_list_cb_t cb, void* ctx);

int vfs_open(const char* path, int flags, vfs_file_t* out_file);
int vfs_close(vfs_file_t* file);
int vfs_read(vfs_file_t* file, void* buffer, size_t size);
int vfs_write(vfs_file_t* file, const void* buffer, size_t size);
int vfs_seek(vfs_file_t* file, int64_t off, int whence, uint64_t* out_pos);
int vfs_stat(const char* path, vfs_stat_t* out_stat);
int vfs_fstat(const vfs_file_t* file, vfs_stat_t* out_stat);
