/**
 * @file devfs.c
 * @brief Minimal devfs implementation for core character device aliases.
 */

#include "devfs.h"
#include "vfs.h"
#include "../../include/common.h"
#include "../../include/error.h"

typedef struct {
    char name[32];
} devfs_pending_block_t;

static vfs_node_t* g_devfs_root = NULL;
static devfs_pending_block_t g_pending_blocks[16];
static uint32_t g_pending_block_count = 0;

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
