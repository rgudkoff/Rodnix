/**
 * @file vfs.c
 * @brief Minimal VFS + RAMFS implementation
 */

#include "vfs.h"
#include "initrd.h"
#include "ext2.h"
#include "devfs.h"
#include "../fabric/service/block_service.h"
#include "../common/tty_console.h"
#include "../common/heap.h"
#include "../common/security.h"
#include "../core/task.h"
#include "../../include/common.h"
#include "../../include/console.h"
#include "../../include/error.h"
#include "../../include/debug.h"

#define VFS_CACHE_SIZE 64

typedef struct vfs_cache_entry {
    char path[64];
    vfs_node_t* node;
    uint32_t gen;
    uint32_t node_gen; /* snapshot of node->inode->node_gen at insert time */
} vfs_cache_entry_t;

/*
 * LOCKING: VFS globals — currently unprotected (single-threaded VFS path).
 *   Protects: vfs_mounts, vfs_root_mount, vfs_root, vfs_ready.
 *   TODO: add a vfs_lock (rwlock or spinlock) before enabling concurrent VFS callers.
 *
 * LOCKING: VFS path cache (vfs_cache[], vfs_cache_gen, vfs_cache_rr).
 *   Currently unprotected — safe because all VFS mutations (vfs_mkdir, vfs_create,
 *   vfs_unlink) call vfs_cache_reset() and the cache is advisory (miss = re-lookup).
 *   On SMP or preemptible kernel: protect with a dedicated spinlock.
 *   cache entries include a node_gen stamp (P1-6A) to detect stale pointers on free.
 */
static vfs_mount_t* vfs_mounts = NULL;
static vfs_mount_t* vfs_root_mount = NULL;
static vfs_node_t* vfs_root = NULL;
static int vfs_ready = 0;
static uint32_t vfs_cache_gen = 1;
static uint32_t vfs_cache_rr = 0;
static vfs_cache_entry_t vfs_cache[VFS_CACHE_SIZE];

static const void* vfs_initrd_data = NULL;
static size_t vfs_initrd_size = 0;

#define VFS_MAX_FS_DRIVERS 8
static const vfs_fs_driver_t* vfs_fs_drivers[VFS_MAX_FS_DRIVERS];
static uint32_t vfs_fs_driver_count = 0;

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
            /* Validate that the node has not been freed and reallocated (P1-6A) */
            vfs_node_t* n = vfs_cache[i].node;
            if (n && n->inode && n->inode->node_gen != vfs_cache[i].node_gen) {
                continue; /* stale entry — node was freed and inode reused */
            }
            return n;
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
    vfs_cache[slot].node_gen = node->inode ? node->inode->node_gen : 0;
}

static vfs_inode_t* vfs_alloc_inode(vfs_node_type_t type)
{
    vfs_inode_t* inode = (vfs_inode_t*)kmalloc(sizeof(vfs_inode_t));
    if (!inode) {
        return NULL;
    }
    memset(inode, 0, sizeof(*inode));
    inode->type = type;
    /* Set default DAC permissions and ownership. */
    inode->mode = (type == VFS_NODE_DIR) ? VFS_MODE_DIR_DEFAULT : VFS_MODE_FILE_DEFAULT;
    task_t* creator = task_get_current();
    if (creator) {
        inode->uid = creator->euid;
        inode->gid = creator->egid;
    }
    return inode;
}

static vfs_node_t* vfs_alloc_node(const char* name, vfs_node_type_t type)
{
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) {
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    node->ref_count = 1; /* tree holds one reference at birth */
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
        node->inode->node_gen++; /* invalidate any cache entries pointing here (P1-6A) */
        if (node->inode->data) {
            kfree(node->inode->data);
        }
        kfree(node->inode);
    }
    kfree(node);
}

/* Increment the reference count of a node.
 * Caller must already hold a valid (non-NULL) pointer to the node. */
static void vfs_node_retain(vfs_node_t* node)
{
    if (node) {
        node->ref_count++;
    }
}

/* Decrement the reference count of a node.
 * When the count reaches zero the node is freed via vfs_free_node.
 * The pointer becomes invalid after this call if it was the last reference. */
static void vfs_node_release(vfs_node_t* node)
{
    if (!node) {
        return;
    }
    if (node->ref_count == 0) {
        /* Double-release bug — log and bail rather than underflow. */
        kprintf("[VFS] vfs_node_release: ref_count already 0 on node '%s'\n",
                node->name);
        return;
    }
    node->ref_count--;
    if (node->ref_count == 0) {
        vfs_free_node(node);
    }
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

static int vfs_resize_file(vfs_file_t* file, size_t new_size)
{
    if (!file || !file->node || file->node->type != VFS_NODE_FILE || !file->node->inode) {
        return RDNX_E_INVALID;
    }

    vfs_inode_t* inode = file->node->inode;
    if ((inode->flags & (VFS_INODE_CONSOLE | VFS_INODE_CHARDEV | VFS_INODE_BLOCKDEV)) != 0) {
        return RDNX_E_UNSUPPORTED;
    }

    if (inode->fs_tag == VFS_FS_TAG_EXT2) {
        int rc = ext2_resize_file(file->node, new_size);
        if (rc != RDNX_OK) {
            return rc;
        }
        if (inode->data && new_size > inode->capacity) {
            if (vfs_grow_file(file->node, new_size) != 0) {
                return RDNX_E_NOMEM;
            }
        }
        if (inode->data && new_size > inode->size) {
            memset(inode->data + inode->size, 0, new_size - inode->size);
        }
        inode->size = new_size;
        if (file->pos > new_size) {
            file->pos = new_size;
        }
        return RDNX_OK;
    }

    size_t old_size = inode->size;
    if (new_size > inode->capacity) {
        if (vfs_grow_file(file->node, new_size) != 0) {
            return RDNX_E_NOMEM;
        }
    }
    if (new_size > old_size && inode->data) {
        memset(inode->data + old_size, 0, new_size - old_size);
    }
    inode->size = new_size;
    if (file->pos > new_size) {
        file->pos = new_size;
    }
    return RDNX_OK;
}

static const vfs_fs_driver_t* vfs_find_fs_driver(const char* fs_name)
{
    if (!fs_name || fs_name[0] == '\0') {
        return NULL;
    }
    for (uint32_t i = 0; i < vfs_fs_driver_count; i++) {
        const vfs_fs_driver_t* d = vfs_fs_drivers[i];
        if (d && d->name && strcmp(d->name, fs_name) == 0) {
            return d;
        }
    }
    return NULL;
}

static int vfs_ramfs_mount(const char* source, vfs_node_t** out_root)
{
    (void)source;
    if (!out_root) {
        return RDNX_E_INVALID;
    }
    *out_root = vfs_alloc_node("/", VFS_NODE_DIR);
    if (!*out_root) {
        return RDNX_E_NOMEM;
    }
    return RDNX_OK;
}

static const vfs_fs_driver_t vfs_ramfs_driver = {
    .name = "ramfs",
    .mount = vfs_ramfs_mount,
};

static vfs_node_t* vfs_create_node(vfs_node_t* parent, const char* name, vfs_node_type_t type)
{
    vfs_node_t* node = vfs_alloc_node(name, type);
    if (!node) {
        return NULL;
    }
    if (vfs_add_child(parent, node) != 0) {
        vfs_node_release(node); /* drops tree ref set at alloc; frees node */
        return NULL;
    }
    vfs_cache_reset();
    return node;
}

static int vfs_mount_root_ramfs(void)
{
    const vfs_fs_driver_t* ramfs = vfs_find_fs_driver("ramfs");
    if (!ramfs || !ramfs->mount) {
        return -1;
    }

    vfs_mount_t* mnt = (vfs_mount_t*)kmalloc(sizeof(vfs_mount_t));
    if (!mnt) {
        return -1;
    }
    memset(mnt, 0, sizeof(*mnt));
    mnt->fs_name = "ramfs";
    vfs_node_t* root = NULL;
    if (ramfs->mount(NULL, &root) != RDNX_OK || !root) {
        kfree(mnt);
        return -1;
    }
    mnt->root = root;
    mnt->mountpoint = NULL;
    mnt->next = vfs_mounts;
    vfs_mounts = mnt;
    vfs_root_mount = mnt;
    vfs_root = mnt->root;
    return 0;
}

static int vfs_mount_devfs(void)
{
    if (!vfs_root) {
        return -1;
    }

    int mkrc = vfs_mkdir("/dev");
    if (mkrc != RDNX_OK) {
        return -1;
    }
    int mrc = vfs_mount("devfs", NULL, "/dev");
    if (mrc != RDNX_OK && mrc != RDNX_E_BUSY) {
        return -1;
    }
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
    int ret = vfs_import_initrd();
    if (ret != 0) {
        return ret;
    }
    if (vfs_mount_devfs() != 0) {
        return -1;
    }
    return 0;
}
int vfs_mount_ramfs(const char* path)
{
    return vfs_mount("ramfs", NULL, path);
}

int vfs_register_fs(const vfs_fs_driver_t* driver)
{
    if (!driver || !driver->name || !driver->mount) {
        return RDNX_E_INVALID;
    }
    if (vfs_find_fs_driver(driver->name)) {
        return RDNX_OK;
    }
    if (vfs_fs_driver_count >= VFS_MAX_FS_DRIVERS) {
        return RDNX_E_NOMEM;
    }
    vfs_fs_drivers[vfs_fs_driver_count++] = driver;
    return RDNX_OK;
}

int vfs_mount(const char* fs_name, const char* source, const char* target)
{
    if (!vfs_ready || !fs_name || !target) {
        return RDNX_E_INVALID;
    }

    const vfs_fs_driver_t* driver = vfs_find_fs_driver(fs_name);
    if (!driver || !driver->mount) {
        return RDNX_E_NOTFOUND;
    }

    vfs_node_t* mountpoint = vfs_lookup(target);
    if (!mountpoint || mountpoint->type != VFS_NODE_DIR) {
        return RDNX_E_NOTFOUND;
    }
    if (vfs_find_mount_at(mountpoint)) {
        return RDNX_E_BUSY;
    }

    vfs_node_t* root = NULL;
    int mrc = driver->mount(source, &root);
    if (mrc != RDNX_OK || !root || root->type != VFS_NODE_DIR) {
        return (mrc == RDNX_OK) ? RDNX_E_GENERIC : mrc;
    }

    vfs_mount_t* mnt = (vfs_mount_t*)kmalloc(sizeof(vfs_mount_t));
    if (!mnt) {
        return RDNX_E_NOMEM;
    }
    memset(mnt, 0, sizeof(*mnt));
    mnt->fs_name = driver->name;
    mnt->root = root;
    mnt->mountpoint = mountpoint;
    mnt->next = vfs_mounts;
    vfs_mounts = mnt;
    vfs_cache_reset();
    return RDNX_OK;
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
        kputs("[VFS] initrd: bad magic\n");
        return -1;
    }
    kprintf("[VFS] initrd: entries=%u\n", (unsigned)hdr->entry_count);

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
        kprintf("[VFS] initrd entry: %s (%u bytes)\n", e->path, (unsigned)e->size);
        size_t end = (size_t)e->offset + (size_t)e->size;
        if (end > vfs_initrd_size) {
            continue;
        }
        /* Ensure parent directories exist */
        char dirbuf[128];
        size_t plen = strlen(e->path);
        if (plen == 0 || plen >= sizeof(dirbuf)) {
            continue;
        }
        memcpy(dirbuf, e->path, plen + 1);
        if (dirbuf[0] != '/') {
            continue;
        }
        for (char* p = dirbuf + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                vfs_mkdir(dirbuf);
                *p = '/';
            }
        }

        char leaf[32];
        vfs_node_t* parent = vfs_resolve_parent(e->path, leaf, sizeof(leaf));
        if (!parent) {
            continue;
        }
        vfs_node_t* node = vfs_find_child(parent, leaf);
        if (!node) {
            node = vfs_create_node(parent, leaf, VFS_NODE_FILE);
        }
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
    (void)vfs_register_fs(&vfs_ramfs_driver);
    (void)devfs_fs_init();
    (void)ext2_fs_init();
    if (vfs_mount_root_ramfs() != 0) {
        return RDNX_E_GENERIC;
    }
    vfs_cache_reset();
    if (vfs_import_initrd() != 0) {
        kputs("[VFS] initrd import failed\n");
    }
    tty_console_init();
    vfs_ready = 1;
    if (vfs_mount_devfs() != 0) {
        kputs("[VFS] devfs mount failed\n");
    }
    return RDNX_OK;
}

int vfs_is_ready(void)
{
    return vfs_ready;
}

int vfs_mkdir(const char* path)
{
    if (!path || !vfs_root) {
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
    if (parent->inode && parent->inode->fs_tag == VFS_FS_TAG_EXT2) {
        return RDNX_E_UNSUPPORTED;
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
    if (node->inode && node->inode->fs_tag == VFS_FS_TAG_EXT2) {
        return RDNX_E_UNSUPPORTED;
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
    /* Mark as removed from namespace before cache reset so any concurrent
     * lookup via the still-live cache sees the flag on the returned node. */
    node->unlinked = true;
    node->parent = NULL;
    vfs_cache_reset();
    vfs_node_release(node); /* drop tree's reference; frees immediately if no open files */
    return RDNX_OK;
}

static int vfs_is_ancestor_dir(const vfs_node_t* ancestor, const vfs_node_t* node)
{
    const vfs_node_t* cur = node;
    while (cur) {
        if (cur == ancestor) {
            return 1;
        }
        cur = cur->parent;
    }
    return 0;
}

int vfs_rename(const char* old_path, const char* new_path)
{
    if (!old_path || !new_path || !vfs_ready) {
        return RDNX_E_INVALID;
    }
    if (strcmp(old_path, "/") == 0 || strcmp(new_path, "/") == 0) {
        return RDNX_E_DENIED;
    }
    if (strcmp(old_path, new_path) == 0) {
        return RDNX_OK;
    }

    char old_leaf[32];
    char new_leaf[32];
    vfs_node_t* old_parent = vfs_resolve_parent(old_path, old_leaf, sizeof(old_leaf));
    vfs_node_t* new_parent = vfs_resolve_parent(new_path, new_leaf, sizeof(new_leaf));
    if (!old_parent || !new_parent || old_leaf[0] == '\0' || new_leaf[0] == '\0') {
        return RDNX_E_NOTFOUND;
    }

    vfs_node_t* node = vfs_find_child(old_parent, old_leaf);
    if (!node) {
        return RDNX_E_NOTFOUND;
    }
    if ((node->inode && node->inode->fs_tag == VFS_FS_TAG_EXT2) ||
        (new_parent->inode && new_parent->inode->fs_tag == VFS_FS_TAG_EXT2)) {
        return RDNX_E_UNSUPPORTED;
    }
    if (vfs_find_child(new_parent, new_leaf)) {
        return RDNX_E_BUSY;
    }
    if (node->type == VFS_NODE_DIR && vfs_is_ancestor_dir(node, new_parent)) {
        return RDNX_E_INVALID;
    }

    vfs_node_t* prev = NULL;
    for (vfs_node_t* it = old_parent->children; it; it = it->sibling) {
        if (it == node) {
            if (prev) {
                prev->sibling = it->sibling;
            } else {
                old_parent->children = it->sibling;
            }
            break;
        }
        prev = it;
    }

    node->sibling = new_parent->children;
    new_parent->children = node;
    node->parent = new_parent;
    strncpy(node->name, new_leaf, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
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
        if (parent->inode && parent->inode->fs_tag == VFS_FS_TAG_EXT2) {
            return RDNX_E_UNSUPPORTED;
        }
        node = vfs_create_node(parent, leaf, VFS_NODE_FILE);
        if (!node) {
            return RDNX_E_NOMEM;
        }
    }
    if (node->type != VFS_NODE_FILE || !node->inode) {
        return RDNX_E_INVALID;
    }

    /* DAC permission check (P2). */
    {
        task_t* caller = task_get_current();
        uint32_t euid = caller ? caller->euid : 0;
        uint32_t egid = caller ? caller->egid : 0;
        int access = SEC_ACCESS_READ;
        if (flags & VFS_OPEN_WRITE) {
            access |= SEC_ACCESS_WRITE;
        }
        /* Device inodes (console, null, zero) are always accessible. */
        bool is_dev = (node->inode->flags &
                       (VFS_INODE_CONSOLE | VFS_INODE_DEV_NULL |
                        VFS_INODE_DEV_ZERO | VFS_INODE_CHARDEV |
                        VFS_INODE_BLOCKDEV)) != 0;
        if (!is_dev && node->inode->mode != 0) {
            if (security_vfs_access(node->inode->mode,
                                    node->inode->uid,
                                    node->inode->gid,
                                    access, euid, egid) != SEC_OK) {
                return RDNX_E_DENIED;
            }
        }
    }

    vfs_node_retain(node);   /* file descriptor holds a reference */
    out_file->node = node;
    out_file->pos = 0;
    out_file->writable = (flags & VFS_OPEN_WRITE) != 0;
    if (flags & VFS_OPEN_TRUNC) {
        int trc = vfs_resize_file(out_file, 0);
        if (trc != RDNX_OK) {
            vfs_node_release(out_file->node); /* undo the retain above */
            out_file->node = NULL;
            out_file->pos = 0;
            out_file->writable = false;
            return trc;
        }
    }
    return RDNX_OK;
}

int vfs_close(vfs_file_t* file)
{
    if (!file) {
        return RDNX_E_INVALID;
    }
    if (file->node) {
        vfs_node_release(file->node); /* drops the reference taken in vfs_open */
        file->node = NULL;
    }
    file->pos = 0;
    file->writable = false;
    return RDNX_OK;
}

int vfs_file_dup(const vfs_file_t* src, vfs_file_t* dst)
{
    if (!src || !dst || !src->node) {
        return RDNX_E_INVALID;
    }
    *dst = *src;                   /* shallow copy — position, flags, node ptr */
    vfs_node_retain(dst->node);    /* dst now holds its own reference */
    return RDNX_OK;
}

int vfs_read(vfs_file_t* file, void* buffer, size_t size)
{
    if (!file || !file->node || !file->node->inode || !buffer) {
        return RDNX_E_INVALID;
    }
    vfs_inode_t* inode = file->node->inode;
    if (inode->flags & VFS_INODE_CONSOLE) {
        return tty_console_read(buffer, size, file->writable);
    }
    if (inode->flags & VFS_INODE_DEV_NULL) {
        return 0;
    }
    if (inode->flags & VFS_INODE_DEV_ZERO) {
        memset(buffer, 0, size);
        return (int)size;
    }
    if (inode->flags & VFS_INODE_BLOCKDEV) {
        fabric_blockdev_t* bdev = fabric_blockdev_find(file->node->name);
        if (!bdev || bdev->sector_size == 0) {
            return RDNX_E_NOTFOUND;
        }
        if (size == 0) {
            return 0;
        }
        uint8_t* out = (uint8_t*)buffer;
        uint32_t sector_size = bdev->sector_size;
        uint8_t* bounce = (uint8_t*)kmalloc(sector_size);
        if (!bounce) {
            return RDNX_E_NOMEM;
        }
        size_t done = 0;
        while (done < size) {
            uint64_t abs_off = (uint64_t)file->pos + (uint64_t)done;
            uint64_t lba = abs_off / (uint64_t)sector_size;
            uint32_t in_sector_off = (uint32_t)(abs_off % (uint64_t)sector_size);
            size_t chunk = size - done;
            size_t max_chunk = (size_t)sector_size - (size_t)in_sector_off;
            if (chunk > max_chunk) {
                chunk = max_chunk;
            }

            int rc = fabric_blockdev_read(bdev, lba, 1, bounce);
            if (rc != RDNX_OK) {
                kfree(bounce);
                return rc;
            }
            memcpy(out + done, bounce + in_sector_off, chunk);
            done += chunk;
        }
        kfree(bounce);
        file->pos += size;
        return (int)size;
    }
    if (file->pos >= inode->size) {
        return 0;
    }
    size_t avail = inode->size - file->pos;
    size_t to_read = size < avail ? size : avail;
    if (!inode->data) {
        if (inode->fs_tag == VFS_FS_TAG_EXT2) {
            int n = ext2_read_file_range(inode->fs_ino, (uint64_t)file->pos, buffer, to_read);
            if (n < 0) {
                return n;
            }
            file->pos += (size_t)n;
            return n;
        }
        return RDNX_E_INVALID;
    }
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
    if (inode->flags & VFS_INODE_CONSOLE) {
        return tty_console_write(buffer, size);
    }
    if (inode->flags & VFS_INODE_DEV_NULL) {
        return (int)size;
    }
    if (inode->flags & VFS_INODE_DEV_ZERO) {
        return (int)size;
    }
    if (inode->fs_tag == VFS_FS_TAG_EXT2) {
        size_t end = file->pos + size;
        size_t final_size = (end > inode->size) ? end : inode->size;
        int wrc = ext2_writeback_file(file->node, file->pos, buffer, size, final_size);
        if (wrc != RDNX_OK) {
            return wrc;
        }
        if (inode->data && end > inode->capacity) {
            if (vfs_grow_file(file->node, end) != 0) {
                return RDNX_E_NOMEM;
            }
        }
        if (end > inode->size) {
            inode->size = end;
        }
        if (inode->data && size > 0) {
            size_t avail = (file->pos < inode->size) ? (inode->size - file->pos) : 0;
            size_t copy = (size < avail) ? size : avail;
            if (copy > 0) {
                memcpy(inode->data + file->pos, buffer, copy);
            }
        }
        file->pos += size;
        return (int)size;
    }
    if (inode->flags & VFS_INODE_BLOCKDEV) {
        fabric_blockdev_t* bdev = fabric_blockdev_find(file->node->name);
        if (!bdev || bdev->sector_size == 0) {
            return RDNX_E_NOTFOUND;
        }
        if (size == 0) {
            return 0;
        }
        if ((file->pos % bdev->sector_size) != 0 || (size % bdev->sector_size) != 0) {
            return RDNX_E_INVALID;
        }
        uint64_t lba = (uint64_t)file->pos / (uint64_t)bdev->sector_size;
        uint32_t count = (uint32_t)(size / bdev->sector_size);
        int rc = fabric_blockdev_write(bdev, lba, count, buffer);
        if (rc != RDNX_OK) {
            return rc;
        }
        file->pos += size;
        return (int)size;
    }
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

int vfs_seek(vfs_file_t* file, int64_t off, int whence, uint64_t* out_pos)
{
    uint64_t base;
    uint64_t end;
    int64_t next;

    if (!file || !file->node || !file->node->inode) {
        return RDNX_E_INVALID;
    }
    if (file->node->inode->flags & VFS_INODE_CONSOLE) {
        return RDNX_E_UNSUPPORTED;
    }

    base = 0;
    end = (uint64_t)file->node->inode->size;
    switch (whence) {
        case 0: /* SEEK_SET */
            base = 0;
            break;
        case 1: /* SEEK_CUR */
            base = (uint64_t)file->pos;
            break;
        case 2: /* SEEK_END */
            base = end;
            break;
        default:
            return RDNX_E_INVALID;
    }

    next = (int64_t)base + off;
    if (next < 0) {
        return RDNX_E_INVALID;
    }
    file->pos = (size_t)next;
    if (out_pos) {
        *out_pos = (uint64_t)file->pos;
    }
    return RDNX_OK;
}

int vfs_truncate(const char* path, uint64_t size)
{
    if (!path || !vfs_ready) {
        return RDNX_E_INVALID;
    }
    if (size > (uint64_t)SIZE_MAX) {
        return RDNX_E_INVALID;
    }

    vfs_node_t* node = vfs_lookup(path);
    if (!node || node->type != VFS_NODE_FILE || !node->inode) {
        return RDNX_E_NOTFOUND;
    }

    vfs_file_t tmp = {
        .node = node,
        .pos = 0,
        .writable = true,
    };
    return vfs_resize_file(&tmp, (size_t)size);
}

int vfs_ftruncate(vfs_file_t* file, uint64_t size)
{
    if (size > (uint64_t)SIZE_MAX) {
        return RDNX_E_INVALID;
    }
    return vfs_resize_file(file, (size_t)size);
}

int vfs_stat(const char* path, vfs_stat_t* out_stat)
{
    vfs_node_t* node;
    if (!path || !out_stat || !vfs_ready) {
        return RDNX_E_INVALID;
    }
    node = vfs_lookup(path);
    if (!node || !node->inode) {
        return RDNX_E_NOTFOUND;
    }

    memset(out_stat, 0, sizeof(*out_stat));
    out_stat->mode = (node->type == VFS_NODE_DIR) ? 0040000u : 0100000u;
    out_stat->size = (uint64_t)node->inode->size;
    return RDNX_OK;
}

int vfs_fstat(const vfs_file_t* file, vfs_stat_t* out_stat)
{
    if (!file || !file->node || !file->node->inode || !out_stat) {
        return RDNX_E_INVALID;
    }
    memset(out_stat, 0, sizeof(*out_stat));
    out_stat->mode = (file->node->type == VFS_NODE_DIR) ? 0040000u : 0100000u;
    out_stat->size = (uint64_t)file->node->inode->size;
    return RDNX_OK;
}

vfs_node_t* vfs_fs_alloc_node(const char* name, vfs_node_type_t type)
{
    return vfs_alloc_node(name, type);
}

int vfs_fs_add_child(vfs_node_t* parent, vfs_node_t* child)
{
    if (!parent || !child) {
        return RDNX_E_INVALID;
    }
    return (vfs_add_child(parent, child) == 0) ? RDNX_OK : RDNX_E_INVALID;
}

int vfs_fs_set_file_data(vfs_node_t* node, const void* data, size_t size)
{
    if (!node || node->type != VFS_NODE_FILE || !node->inode) {
        return RDNX_E_INVALID;
    }
    if (size > 0 && !data) {
        return RDNX_E_INVALID;
    }
    if (vfs_grow_file(node, size) != 0) {
        return RDNX_E_NOMEM;
    }
    if (size > 0) {
        memcpy(node->inode->data, data, size);
    }
    node->inode->size = size;
    return RDNX_OK;
}

void vfs_fs_free_node(vfs_node_t* node)
{
    vfs_node_release(node);
}
