/**
 * @file vfs.c
 * @brief Minimal VFS + RAMFS implementation
 */

#include "vfs.h"
#include "../common/heap.h"
#include "../../include/common.h"
#include "../../include/console.h"

static vfs_node_t* vfs_root = NULL;
static int vfs_ready = 0;

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
    return node;
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

    vfs_node_t* current = vfs_root;
    char comp[32];
    const char* p = path;
    const char* next;

    while ((next = vfs_next_component(p, comp, sizeof(comp))) != NULL) {
        current = vfs_find_child(current, comp);
        if (!current) {
            return NULL;
        }
        p = next;
    }
    return current;
}

static int vfs_grow_file(vfs_node_t* node, size_t needed)
{
    if (!node || node->type != VFS_NODE_FILE) {
        return -1;
    }
    if (needed <= node->capacity) {
        return 0;
    }
    size_t new_cap = node->capacity ? node->capacity : 64;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    uint8_t* new_buf = (uint8_t*)kmalloc(new_cap);
    if (!new_buf) {
        return -1;
    }
    if (node->data && node->size > 0) {
        memcpy(new_buf, node->data, node->size);
    }
    if (node->data) {
        kfree(node->data);
    }
    node->data = new_buf;
    node->capacity = new_cap;
    return 0;
}

int vfs_init(void)
{
    if (vfs_ready) {
        return 0;
    }
    vfs_root = vfs_alloc_node("/", VFS_NODE_DIR);
    if (!vfs_root) {
        return -1;
    }
    vfs_ready = 1;
    return 0;
}

int vfs_is_ready(void)
{
    return vfs_ready;
}

int vfs_mkdir(const char* path)
{
    if (!path || !vfs_ready) {
        return -1;
    }
    if (strcmp(path, "/") == 0) {
        return 0;
    }
    char leaf[32];
    vfs_node_t* parent = vfs_resolve_parent(path, leaf, sizeof(leaf));
    if (!parent) {
        return -1;
    }
    if (vfs_find_child(parent, leaf)) {
        return 0;
    }
    vfs_node_t* dir = vfs_alloc_node(leaf, VFS_NODE_DIR);
    if (!dir) {
        return -1;
    }
    return vfs_add_child(parent, dir);
}

int vfs_unlink(const char* path)
{
    if (!path || !vfs_ready) {
        return -1;
    }
    vfs_node_t* node = vfs_lookup(path);
    if (!node || node == vfs_root) {
        return -1;
    }
    if (node->type == VFS_NODE_DIR && node->children) {
        return -1;
    }
    vfs_node_t* parent = node->parent;
    if (!parent) {
        return -1;
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
    if (node->data) {
        kfree(node->data);
    }
    kfree(node);
    return 0;
}

int vfs_list_dir(const char* path, vfs_list_cb_t cb, void* ctx)
{
    if (!path || !cb || !vfs_ready) {
        return -1;
    }
    vfs_node_t* dir = vfs_lookup(path);
    if (!dir || dir->type != VFS_NODE_DIR) {
        return -1;
    }
    for (vfs_node_t* it = dir->children; it; it = it->sibling) {
        cb(it, ctx);
    }
    return 0;
}

int vfs_open(const char* path, int flags, vfs_file_t* out_file)
{
    if (!path || !out_file || !vfs_ready) {
        return -1;
    }
    vfs_node_t* node = vfs_lookup(path);
    if (!node) {
        if (!(flags & VFS_OPEN_CREATE)) {
            return -1;
        }
        char leaf[32];
        vfs_node_t* parent = vfs_resolve_parent(path, leaf, sizeof(leaf));
        if (!parent) {
            return -1;
        }
        node = vfs_alloc_node(leaf, VFS_NODE_FILE);
        if (!node) {
            return -1;
        }
        if (vfs_add_child(parent, node) != 0) {
            kfree(node);
            return -1;
        }
    }
    if (node->type != VFS_NODE_FILE) {
        return -1;
    }
    if (flags & VFS_OPEN_TRUNC) {
        node->size = 0;
    }
    out_file->node = node;
    out_file->pos = 0;
    out_file->writable = (flags & VFS_OPEN_WRITE) != 0;
    return 0;
}

int vfs_close(vfs_file_t* file)
{
    if (!file) {
        return -1;
    }
    file->node = NULL;
    file->pos = 0;
    file->writable = false;
    return 0;
}

int vfs_read(vfs_file_t* file, void* buffer, size_t size)
{
    if (!file || !file->node || !buffer) {
        return -1;
    }
    vfs_node_t* node = file->node;
    if (file->pos >= node->size) {
        return 0;
    }
    size_t avail = node->size - file->pos;
    size_t to_read = size < avail ? size : avail;
    memcpy(buffer, node->data + file->pos, to_read);
    file->pos += to_read;
    return (int)to_read;
}

int vfs_write(vfs_file_t* file, const void* buffer, size_t size)
{
    if (!file || !file->node || !buffer || !file->writable) {
        return -1;
    }
    vfs_node_t* node = file->node;
    size_t end = file->pos + size;
    if (vfs_grow_file(node, end) != 0) {
        return -1;
    }
    memcpy(node->data + file->pos, buffer, size);
    file->pos += size;
    if (end > node->size) {
        node->size = end;
    }
    return (int)size;
}
