/**
 * @file devfs.c
 * @brief Minimal devfs implementation for core character device aliases.
 */

#include "devfs.h"
#include "vfs.h"
#include "../../include/common.h"
#include "../../include/error.h"
#include "../lib/heap.h"

typedef struct {
    char name[32];
} devfs_pending_block_t;

typedef struct {
    char     name[32];
    uint32_t display_idx;
    uint64_t phys_base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  pixel_format;
    uint8_t  _pad[2];
    uint64_t size;
} devfs_pending_fb_t;

static vfs_node_t*         g_devfs_root = NULL;
static devfs_pending_block_t g_pending_blocks[16];
static uint32_t              g_pending_block_count = 0;
static devfs_pending_fb_t    g_pending_fbs[4];
static uint32_t              g_pending_fb_count = 0;

static vfs_node_t* devfs_find_child(vfs_node_t* root, const char* name)
{
    if (!root || !name || root->type != VFS_NODE_DIR) {
        return NULL;
    }
    for (vfs_node_t* it = root->children; it; it = it->sibling) {
        if (strcmp(it->name, name) == 0) {
            return it;
        }
    }
    return NULL;
}

static int devfs_add_chardev(vfs_node_t* root, const char* name, uint32_t extra_flags)
{
    vfs_node_t* node = vfs_fs_alloc_node(name, VFS_NODE_FILE);
    if (!node || !node->inode) {
        return RDNX_E_NOMEM;
    }
    node->inode->flags |= VFS_INODE_CHARDEV | extra_flags;
    int rc = vfs_fs_add_child(root, node);
    if (rc != RDNX_OK) {
        vfs_fs_free_node(node);
        return rc;
    }
    return RDNX_OK;
}

static int devfs_add_blockdev(vfs_node_t* root, const char* name)
{
    if (!root || !name || !name[0]) {
        return RDNX_E_INVALID;
    }
    if (devfs_find_child(root, name)) {
        return RDNX_OK;
    }
    vfs_node_t* node = vfs_fs_alloc_node(name, VFS_NODE_FILE);
    if (!node || !node->inode) {
        return RDNX_E_NOMEM;
    }
    node->inode->flags |= VFS_INODE_BLOCKDEV;
    int rc = vfs_fs_add_child(root, node);
    if (rc != RDNX_OK) {
        vfs_fs_free_node(node);
        return rc;
    }
    return RDNX_OK;
}

static int devfs_add_framebuffer_node(vfs_node_t* root,
                                      const devfs_pending_fb_t* pf)
{
    if (devfs_find_child(root, pf->name)) {
        return RDNX_OK;
    }
    vfs_node_t* node = vfs_fs_alloc_node(pf->name, VFS_NODE_FILE);
    if (!node || !node->inode) {
        return RDNX_E_NOMEM;
    }
    fb_dev_info_t* info = (fb_dev_info_t*)kmalloc(sizeof(fb_dev_info_t));
    if (!info) {
        vfs_fs_free_node(node);
        return RDNX_E_NOMEM;
    }
    info->width        = pf->width;
    info->height       = pf->height;
    info->pitch        = pf->pitch;
    info->bpp          = pf->bpp;
    info->pixel_format = pf->pixel_format;
    info->_pad[0]      = 0;
    info->_pad[1]      = 0;
    info->phys_base    = pf->phys_base;
    info->size         = pf->size;

    node->inode->flags    = VFS_INODE_FRAMEBUFFER;
    node->inode->fs_aux   = pf->display_idx;
    node->inode->data     = (uint8_t*)info;
    node->inode->size     = sizeof(fb_dev_info_t);
    node->inode->capacity = sizeof(fb_dev_info_t);

    int rc = vfs_fs_add_child(root, node);
    if (rc != RDNX_OK) {
        kfree(info);
        vfs_fs_free_node(node);
        return rc;
    }
    return RDNX_OK;
}

static int devfs_mount(const char* source, vfs_node_t** out_root)
{
    (void)source;
    if (!out_root) {
        return RDNX_E_INVALID;
    }

    vfs_node_t* root = vfs_fs_alloc_node("/", VFS_NODE_DIR);
    if (!root) {
        return RDNX_E_NOMEM;
    }

    static const char* aliases[] = {
        "console",
        "stdin",
        "stdout",
        "stderr",
    };

    for (size_t i = 0; i < (sizeof(aliases) / sizeof(aliases[0])); i++) {
        int rc = devfs_add_chardev(root, aliases[i], VFS_INODE_CONSOLE);
        if (rc != RDNX_OK) {
            return rc;
        }
    }
    if (devfs_add_chardev(root, "null", VFS_INODE_DEV_NULL) != RDNX_OK) {
        return RDNX_E_NOMEM;
    }
    if (devfs_add_chardev(root, "zero", VFS_INODE_DEV_ZERO) != RDNX_OK) {
        return RDNX_E_NOMEM;
    }
    for (uint32_t i = 0; i < g_pending_block_count; i++) {
        if (devfs_add_blockdev(root, g_pending_blocks[i].name) != RDNX_OK) {
            return RDNX_E_NOMEM;
        }
    }
    for (uint32_t i = 0; i < g_pending_fb_count; i++) {
        if (devfs_add_framebuffer_node(root, &g_pending_fbs[i]) != RDNX_OK) {
            return RDNX_E_NOMEM;
        }
    }

    g_devfs_root = root;
    *out_root = root;
    return RDNX_OK;
}

static const vfs_fs_driver_t devfs_driver = {
    .name = "devfs",
    .mount = devfs_mount,
};

int devfs_fs_init(void)
{
    return vfs_register_fs(&devfs_driver);
}

int devfs_register_framebuffer(const char* name,
                               uint32_t display_idx,
                               uint64_t phys_base,
                               uint32_t width, uint32_t height,
                               uint32_t pitch, uint8_t bpp,
                               uint8_t pixel_format,
                               uint64_t size)
{
    if (!name || !name[0] || phys_base == 0) {
        return RDNX_E_INVALID;
    }

    if (g_devfs_root) {
        /* devfs already mounted — register immediately. */
        if (devfs_find_child(g_devfs_root, name)) {
            return RDNX_OK;
        }
        devfs_pending_fb_t pf;
        strncpy(pf.name, name, sizeof(pf.name) - 1);
        pf.name[sizeof(pf.name) - 1] = '\0';
        pf.display_idx  = display_idx;
        pf.phys_base    = phys_base;
        pf.width        = width;
        pf.height       = height;
        pf.pitch        = pitch;
        pf.bpp          = bpp;
        pf.pixel_format = pixel_format;
        pf._pad[0]      = 0;
        pf._pad[1]      = 0;
        pf.size         = size;
        return devfs_add_framebuffer_node(g_devfs_root, &pf);
    }

    /* devfs not yet mounted — queue for registration at mount time. */
    if (g_pending_fb_count >= (sizeof(g_pending_fbs) / sizeof(g_pending_fbs[0]))) {
        return RDNX_E_BUSY;
    }
    devfs_pending_fb_t* pf = &g_pending_fbs[g_pending_fb_count];
    strncpy(pf->name, name, sizeof(pf->name) - 1);
    pf->name[sizeof(pf->name) - 1] = '\0';
    pf->display_idx  = display_idx;
    pf->phys_base    = phys_base;
    pf->width        = width;
    pf->height       = height;
    pf->pitch        = pitch;
    pf->bpp          = bpp;
    pf->pixel_format = pixel_format;
    pf->_pad[0]      = 0;
    pf->_pad[1]      = 0;
    pf->size         = size;
    g_pending_fb_count++;
    return RDNX_OK;
}

int devfs_register_blockdev(const char* name)
{
    if (!name || !name[0]) {
        return RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < g_pending_block_count; i++) {
        if (strcmp(g_pending_blocks[i].name, name) == 0) {
            return RDNX_OK;
        }
    }

    if (g_devfs_root) {
        return devfs_add_blockdev(g_devfs_root, name);
    }

    if (g_pending_block_count >= (sizeof(g_pending_blocks) / sizeof(g_pending_blocks[0]))) {
        return RDNX_E_BUSY;
    }
    strncpy(g_pending_blocks[g_pending_block_count].name, name,
            sizeof(g_pending_blocks[g_pending_block_count].name) - 1);
    g_pending_blocks[g_pending_block_count].name[sizeof(g_pending_blocks[g_pending_block_count].name) - 1] = '\0';
    g_pending_block_count++;
    return RDNX_OK;
}
