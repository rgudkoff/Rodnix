/**
 * @file ext2.c
 * @brief Minimal EXT2 mount driver (skeleton, read-only placeholder)
 */

#include "ext2.h"
#include "vfs.h"
#include "../fabric/service/block_service.h"
#include "../../../include/common.h"
#include "../../include/error.h"

typedef struct __attribute__((packed)) {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t r_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
} ext2_superblock_t;

static void append_u64(char* buf, uint32_t cap, uint64_t v)
{
    char tmp[32];
    uint32_t n = 0;
    if (cap == 0) {
        return;
    }
    if (v == 0) {
        size_t l = strlen(buf);
        if (l + 1 < cap) {
            buf[l] = '0';
            buf[l + 1] = '\0';
        }
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        size_t l = strlen(buf);
        if (l + 1 >= cap) {
            return;
        }
        buf[l] = tmp[--n];
        buf[l + 1] = '\0';
    }
}

static void append_str(char* buf, uint32_t cap, const char* s)
{
    if (!buf || cap == 0 || !s) {
        return;
    }
    size_t l = strlen(buf);
    size_t i = 0;
    while (s[i] && l + 1 < cap) {
        buf[l++] = s[i++];
    }
    buf[l] = '\0';
}

static int ext2_mount(const char* source, vfs_node_t** out_root)
{
    const char* disk_name = (source && source[0]) ? source : "disk0";
    if (!out_root) {
        return RDNX_E_INVALID;
    }

    fabric_blockdev_t* bdev = fabric_blockdev_find(disk_name);
    if (!bdev) {
        return RDNX_E_NOTFOUND;
    }
    if (bdev->sector_size != 512) {
        return RDNX_E_UNSUPPORTED;
    }

    uint8_t super_raw[1024];
    if (fabric_blockdev_read(bdev, 2, 2, super_raw) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    const ext2_superblock_t* sb = (const ext2_superblock_t*)super_raw;
    if (sb->magic != 0xEF53u) {
        return RDNX_E_INVALID;
    }

    vfs_node_t* root = vfs_fs_alloc_node("/", VFS_NODE_DIR);
    if (!root) {
        return RDNX_E_NOMEM;
    }

    vfs_node_t* lost_found = vfs_fs_alloc_node("lost+found", VFS_NODE_DIR);
    if (!lost_found) {
        return RDNX_E_NOMEM;
    }
    if (vfs_fs_add_child(root, lost_found) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }

    vfs_node_t* readme = vfs_fs_alloc_node("README.ext2", VFS_NODE_FILE);
    if (!readme) {
        return RDNX_E_NOMEM;
    }
    char note[256];
    memset(note, 0, sizeof(note));
    append_str(note, sizeof(note), "EXT2 mounted from ");
    append_str(note, sizeof(note), disk_name);
    append_str(note, sizeof(note), "\nblocks=");
    append_u64(note, sizeof(note), (uint64_t)sb->blocks_count);
    append_str(note, sizeof(note), " inodes=");
    append_u64(note, sizeof(note), (uint64_t)sb->inodes_count);
    append_str(note, sizeof(note), " block_size=");
    append_u64(note, sizeof(note), (uint64_t)(1024u << sb->log_block_size));
    append_str(note, sizeof(note), "\nNOTE: inode/dir traversal is not implemented yet.\n");
    if (vfs_fs_set_file_data(readme, note, strlen(note)) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    if (vfs_fs_add_child(root, readme) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }

    *out_root = root;
    return RDNX_OK;
}

static const vfs_fs_driver_t ext2_driver = {
    .name = "ext2",
    .mount = ext2_mount,
};

int ext2_fs_init(void)
{
    return vfs_register_fs(&ext2_driver);
}
