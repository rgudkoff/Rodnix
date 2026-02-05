/**
 * @file vfs.c
 * @brief Minimal VFS + RAMFS implementation
 */

#include "vfs.h"
#include "initrd.h"
#include "../common/heap.h"
#include "../../include/common.h"
#include "../../include/console.h"
#include "../../include/error.h"
#include "../../include/debug.h"

#define VFS_CACHE_SIZE 64

typedef struct vfs_cache_entry {
    char path[64];
    vfs_node_t* node;
    uint32_t gen;
} vfs_cache_entry_t;

static vfs_mount_t* vfs_mounts = NULL;
static vfs_mount_t* vfs_root_mount = NULL;
static vfs_node_t* vfs_root = NULL;
static int vfs_ready = 0;
static uint32_t vfs_cache_gen = 1;
static uint32_t vfs_cache_rr = 0;
static vfs_cache_entry_t vfs_cache[VFS_CACHE_SIZE];

static const void* vfs_initrd_data = NULL;
static size_t vfs_initrd_size = 0;

static vfs_mount_t* vfs_find_mount_at(const vfs_node_t* node)
{
    for (vfs_mount_t* it = vfs_mounts; it; it = it->next) {
        if (it->mountpoint == node) {
            return it;
        }
    }
    return NULL;
}

static void vfs_cache_reset(void)
{
    vfs_cache_gen++;
    for (size_t i = 0; i < VFS_CACHE_SIZE; i++) {
        vfs_cache[i].path[0] = '\0';
        vfs_cache[i].node = NULL;
        vfs_cache[i].gen = 0;
    }
    vfs_cache_rr = 0;
}

static vfs_node_t* vfs_cache_lookup(const char* path)
{
    if (!path) {
        return NULL;
    }
    for (size_t i = 0; i < VFS_CACHE_SIZE; i++) {
        if (vfs_cache[i].gen != vfs_cache_gen) {
            continue;
        }
        if (vfs_cache[i].path[0] != '\0' && strcmp(vfs_cache[i].path, path) == 0) {
            return vfs_cache[i].node;
        }
    }
    return NULL;
}

static void vfs_cache_insert(const char* path, vfs_node_t* node)
{
    if (!path || !node) {
        return;
    }
    size_t slot = vfs_cache_rr++ % VFS_CACHE_SIZE;
    strncpy(vfs_cache[slot].path, path, sizeof(vfs_cache[slot].path) - 1);
    vfs_cache[slot].path[sizeof(vfs_cache[slot].path) - 1] = '\0';
    vfs_cache[slot].node = node;
    vfs_cache[slot].gen = vfs_cache_gen;
}

static vfs_inode_t* vfs_alloc_inode(vfs_node_type_t type)
{
    vfs_inode_t* inode = (vfs_inode_t*)kmalloc(sizeof(vfs_inode_t));
    if (!inode) {
        return NULL;
    }
    memset(inode, 0, sizeof(*inode));
    inode->type = type;
    return inode;
}

static vfs_node_t* vfs_alloc_node(const char* name, vfs_node_type_t type)
{
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) {
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    if (name) {
        strncpy(node->name, name, sizeof(node->name) - 1);
        node->name[sizeof(node->name) - 1] = '\0';
    }
    node->type = type;
    node->inode = vfs_alloc_inode(type);
    if (!node->inode) {
        kfree(node);
        return NULL;
    }
    return node;
}

static void vfs_free_node(vfs_node_t* node)
{
    if (!node) {
        return;
    }
    if (node->inode) {
        if (node->inode->data) {
            kfree(node->inode->data);
        }
        kfree(node->inode);
    }
    kfree(node);
}

static vfs_node_t* vfs_find_child(vfs_node_t* dir, const char* name)
{
    if (!dir || dir->type != VFS_NODE_DIR) {
        return NULL;
    }
    for (vfs_node_t* it = dir->children; it; it = it->sibling) {
        if (strcmp(it->name, name) == 0) {
            return it;
        }
    }
    return NULL;
}

static int vfs_add_child(vfs_node_t* dir, vfs_node_t* child)
{
    if (!dir || !child || dir->type != VFS_NODE_DIR) {
        return -1;
    }
    child->parent = dir;
    child->sibling = dir->children;
    dir->children = child;
    return 0;
}

static const char* vfs_next_component(const char* path, char* out, size_t out_len)
{
    size_t i = 0;
    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        return NULL;
    }
    while (*path && *path != '/' && i + 1 < out_len) {
        out[i++] = *path++;
    }
    out[i] = '\0';
    while (*path && *path != '/') {
        path++;
    }
    return path;
}

static vfs_node_t* vfs_resolve_parent(const char* path, char* leaf, size_t leaf_len)
{
    if (!vfs_root || !path || !leaf || leaf_len == 0) {
        return NULL;
    }
    if (strcmp(path, "/") == 0) {
        return NULL;
    }

    vfs_node_t* current = vfs_root;
    char comp[32];
    const char* p = path;
    const char* next = NULL;

    while ((next = vfs_next_component(p, comp, sizeof(comp))) != NULL) {
        const char* after = next;
        while (*after == '/') {
            after++;
        }
        if (*after == '\0') {
            strncpy(leaf, comp, leaf_len - 1);
            leaf[leaf_len - 1] = '\0';
            return current;
        }
        current = vfs_find_child(current, comp);
        if (!current || current->type != VFS_NODE_DIR) {
            return NULL;
        }
        vfs_mount_t* mnt = vfs_find_mount_at(current);
        if (mnt && mnt->root) {
            current = mnt->root;
        }
        p = next;
    }
    return NULL;
}

static vfs_node_t* vfs_lookup(const char* path)
{
    if (!vfs_root || !path) {
        return NULL;
    }
    if (strcmp(path, "/") == 0) {
        return vfs_root;
    }

    vfs_node_t* cached = vfs_cache_lookup(path);
    if (cached) {
        return cached;
    }

    vfs_node_t* current = vfs_root;
    char comp[32];
    const char* p = path;
    const char* next;

    while ((next = vfs_next_component(p, comp, sizeof(comp))) != NULL) {
        current = vfs_find_child(current, comp);
        if (!current) {
            return NULL;
        }
        vfs_mount_t* mnt = vfs_find_mount_at(current);
        if (mnt && mnt->root) {
            current = mnt->root;
        }
        p = next;
    }
    vfs_cache_insert(path, current);
    return current;
}

static int vfs_grow_file(vfs_node_t* node, size_t needed)
{
    if (!node || node->type != VFS_NODE_FILE || !node->inode) {
        return -1;
    }
    if (needed <= node->inode->capacity) {
        return 0;
    }
    size_t new_cap = node->inode->capacity ? node->inode->capacity : 64;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    uint8_t* new_buf = (uint8_t*)kmalloc(new_cap);
    if (!new_buf) {
        return -1;
    }
    if (node->inode->data && node->inode->size > 0) {
        memcpy(new_buf, node->inode->data, node->inode->size);
    }
    if (node->inode->data) {
        kfree(node->inode->data);
    }
    node->inode->data = new_buf;
    node->inode->capacity = new_cap;
    return 0;
}

static vfs_node_t* vfs_create_node(vfs_node_t* parent, const char* name, vfs_node_type_t type)
{
    vfs_node_t* node = vfs_alloc_node(name, type);
    if (!node) {
        return NULL;
    }
    if (vfs_add_child(parent, node) != 0) {
        vfs_free_node(node);
        return NULL;
    }
    vfs_cache_reset();
    return node;
}

static int vfs_mount_root_ramfs(void)
{
    vfs_mount_t* mnt = (vfs_mount_t*)kmalloc(sizeof(vfs_mount_t));
    if (!mnt) {
        return -1;
    }
    memset(mnt, 0, sizeof(*mnt));
    mnt->fs_name = "ramfs";
    mnt->root = vfs_alloc_node("/", VFS_NODE_DIR);
    if (!mnt->root) {
        kfree(mnt);
        return -1;
    }
    mnt->mountpoint = NULL;
    mnt->next = vfs_mounts;
    vfs_mounts = mnt;
    vfs_root_mount = mnt;
    vfs_root = mnt->root;
    return 0;
}

static int vfs_import_initrd(void);

int vfs_mount_initrd_root(void)
{
    if (!vfs_ready || !vfs_initrd_data || vfs_initrd_size == 0) {
        return -1;
    }
    /* Replace root mount with initrd-backed RAMFS */
    if (!vfs_root_mount) {
        return -1;
    }
    vfs_node_t* new_root = vfs_alloc_node("/", VFS_NODE_DIR);
    if (!new_root) {
        return -1;
    }
    vfs_root_mount->root = new_root;
    vfs_root = new_root;
    vfs_cache_reset();
    return vfs_import_initrd();
}
int vfs_mount_ramfs(const char* path)
{
    if (!vfs_ready || !path) {
        return -1;
    }

    vfs_node_t* mountpoint = vfs_lookup(path);
    if (!mountpoint || mountpoint->type != VFS_NODE_DIR) {
        return -1;
    }
    if (vfs_find_mount_at(mountpoint)) {
        return -1;
    }

    vfs_mount_t* mnt = (vfs_mount_t*)kmalloc(sizeof(vfs_mount_t));
    if (!mnt) {
        return -1;
    }
    memset(mnt, 0, sizeof(*mnt));
    mnt->fs_name = "ramfs";
    mnt->root = vfs_alloc_node("/", VFS_NODE_DIR);
    if (!mnt->root) {
        kfree(mnt);
        return -1;
    }
    mnt->mountpoint = mountpoint;
    mnt->next = vfs_mounts;
    vfs_mounts = mnt;
    vfs_cache_reset();
    return 0;
}

static int vfs_import_initrd(void)
{
    if (!vfs_root || !vfs_initrd_data || vfs_initrd_size == 0) {
        return 0;
    }
    if (vfs_initrd_size < sizeof(initrd_header_t)) {
        return -1;
    }

    const initrd_header_t* hdr = (const initrd_header_t*)vfs_initrd_data;
    if (hdr->magic != INITRD_MAGIC) {
        return -1;
    }

    const uint8_t* base = (const uint8_t*)vfs_initrd_data;
    size_t entries_size = hdr->entry_count * sizeof(initrd_entry_t);
    size_t table_end = sizeof(initrd_header_t) + entries_size;
    if (table_end > vfs_initrd_size) {
        return -1;
    }

    const initrd_entry_t* entries = (const initrd_entry_t*)(base + sizeof(initrd_header_t));
    for (uint32_t i = 0; i < hdr->entry_count; i++) {
        const initrd_entry_t* e = &entries[i];
        if (e->path[0] == '\0') {
            continue;
        }
        size_t end = (size_t)e->offset + (size_t)e->size;
        if (end > vfs_initrd_size) {
            continue;
        }
        vfs_node_t* node = vfs_create_node(vfs_root, e->path, VFS_NODE_FILE);
        if (!node) {
            continue;
        }
        if (vfs_grow_file(node, e->size) != 0) {
            continue;
        }
        memcpy(node->inode->data, base + e->offset, e->size);
        node->inode->size = e->size;
    }

    return 0;
}

void vfs_set_initrd(const void* data, size_t size)
{
    vfs_initrd_data = data;
    vfs_initrd_size = size;
}

int vfs_init(void)
{
    if (vfs_ready) {
        return RDNX_OK;
    }
    TRACE_EVENT("vfs_init");
    if (vfs_mount_root_ramfs() != 0) {
        return RDNX_E_GENERIC;
    }
    vfs_cache_reset();
    if (vfs_import_initrd() != 0) {
        kputs("[VFS] initrd import failed\n");
    }
    vfs_ready = 1;
    return RDNX_OK;
}

int vfs_is_ready(void)
{
    return vfs_ready;
}

int vfs_mkdir(const char* path)
{
    if (!path || !vfs_ready) {
        return RDNX_E_INVALID;
    }
    if (strcmp(path, "/") == 0) {
        return RDNX_OK;
    }
    char leaf[32];
    vfs_node_t* parent = vfs_resolve_parent(path, leaf, sizeof(leaf));
    if (!parent) {
        return RDNX_E_NOTFOUND;
    }
    if (vfs_find_child(parent, leaf)) {
        return RDNX_OK;
    }
    return vfs_create_node(parent, leaf, VFS_NODE_DIR) ? RDNX_OK : RDNX_E_NOMEM;
}

int vfs_unlink(const char* path)
{
    if (!path || !vfs_ready) {
        return RDNX_E_INVALID;
    }
    vfs_node_t* node = vfs_lookup(path);
    if (!node || node == vfs_root) {
        return RDNX_E_NOTFOUND;
    }
    if (node->type == VFS_NODE_DIR && node->children) {
        return RDNX_E_BUSY;
    }
    vfs_node_t* parent = node->parent;
    if (!parent) {
        return RDNX_E_INVALID;
    }
    vfs_node_t* prev = NULL;
    for (vfs_node_t* it = parent->children; it; it = it->sibling) {
        if (it == node) {
            if (prev) {
                prev->sibling = it->sibling;
            } else {
                parent->children = it->sibling;
            }
            break;
        }
        prev = it;
    }
    vfs_free_node(node);
    vfs_cache_reset();
    return RDNX_OK;
}

int vfs_list_dir(const char* path, vfs_list_cb_t cb, void* ctx)
{
    if (!path || !cb || !vfs_ready) {
        return RDNX_E_INVALID;
    }
    vfs_node_t* dir = vfs_lookup(path);
    if (!dir || dir->type != VFS_NODE_DIR) {
        return RDNX_E_NOTFOUND;
    }
    for (vfs_node_t* it = dir->children; it; it = it->sibling) {
        cb(it, ctx);
    }
    return RDNX_OK;
}

int vfs_open(const char* path, int flags, vfs_file_t* out_file)
{
    if (!path || !out_file || !vfs_ready) {
        return RDNX_E_INVALID;
    }
    TRACE_EVENT("vfs_open");
    vfs_node_t* node = vfs_lookup(path);
    if (!node) {
        if (!(flags & VFS_OPEN_CREATE)) {
            return RDNX_E_NOTFOUND;
        }
        char leaf[32];
        vfs_node_t* parent = vfs_resolve_parent(path, leaf, sizeof(leaf));
        if (!parent) {
            return RDNX_E_NOTFOUND;
        }
        node = vfs_create_node(parent, leaf, VFS_NODE_FILE);
        if (!node) {
            return RDNX_E_NOMEM;
        }
    }
    if (node->type != VFS_NODE_FILE || !node->inode) {
        return RDNX_E_INVALID;
    }
    if (flags & VFS_OPEN_TRUNC) {
        node->inode->size = 0;
    }
    out_file->node = node;
    out_file->pos = 0;
    out_file->writable = (flags & VFS_OPEN_WRITE) != 0;
    return RDNX_OK;
}

int vfs_close(vfs_file_t* file)
{
    if (!file) {
        return RDNX_E_INVALID;
    }
    file->node = NULL;
    file->pos = 0;
    file->writable = false;
    return RDNX_OK;
}

int vfs_read(vfs_file_t* file, void* buffer, size_t size)
{
    if (!file || !file->node || !file->node->inode || !buffer) {
        return RDNX_E_INVALID;
    }
    vfs_inode_t* inode = file->node->inode;
    if (file->pos >= inode->size) {
        return 0;
    }
    size_t avail = inode->size - file->pos;
    size_t to_read = size < avail ? size : avail;
    memcpy(buffer, inode->data + file->pos, to_read);
    file->pos += to_read;
    return (int)to_read;
}

int vfs_write(vfs_file_t* file, const void* buffer, size_t size)
{
    if (!file || !file->node || !file->node->inode || !buffer || !file->writable) {
        return RDNX_E_INVALID;
    }
    vfs_inode_t* inode = file->node->inode;
    size_t end = file->pos + size;
    if (vfs_grow_file(file->node, end) != 0) {
        return RDNX_E_NOMEM;
    }
    memcpy(inode->data + file->pos, buffer, size);
    file->pos += size;
    if (end > inode->size) {
        inode->size = end;
    }
    return (int)size;
}
