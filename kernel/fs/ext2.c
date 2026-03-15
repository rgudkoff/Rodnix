/**
 * @file ext2.c
 * @brief Read-only EXT2 mount driver (inode/dir traversal)
 *
 * Structures and traversal follow a compact loader-oriented EXT2 layout
 * adapted for RodNIX.
 */

#include "ext2.h"
#include "vfs.h"
#include "../common/heap.h"
#include "../common/kmod.h"
#include "../fabric/spin.h"
#include "../fabric/service/block_service.h"
#include "../../../include/common.h"
#include "../../include/console.h"
#include "../../include/error.h"

#include <stdbool.h>
#include <stdint.h>

#define EXT2_MAGIC 0xEF53u
#define EXT2_ROOT_INO 2u

#define EXT2_S_IFDIR 0x4000u
#define EXT2_S_IFREG 0x8000u
#define EXT2_NDIR_BLOCKS 12u

#define EXT2_MAX_BLOCK_SIZE 4096u
#define EXT2_MAX_INODE_SIZE 512u
#define EXT2_MAX_TREE_DEPTH 4u
#define EXT2_MAX_TREE_NODES 2048u
#define EXT2_MAX_FILE_BYTES (64u * 1024u * 1024u) /* preload cap; files > 64 MB truncated at mount */

#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002u
#define EXT2_FEATURE_INCOMPAT_SUPP (EXT2_FEATURE_INCOMPAT_FILETYPE)

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
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
} ext2_superblock_t;

typedef struct __attribute__((packed)) {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint32_t reserved[3];
} ext2_group_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t mode;
    uint16_t uid;
    uint32_t size_lo;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[15];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t size_high;
    uint32_t faddr;
    uint8_t osd2[12];
} ext2_inode_t;

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
} ext2_dirent_hdr_t;

typedef struct {
    fabric_blockdev_t* bdev;
    ext2_superblock_t sb;
    ext2_group_desc_t* gdt;
    uint32_t group_count;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t sector_size;
    uint32_t node_budget;
} ext2_mount_ctx_t;

/*
 * LOCKING: g_ext2_rw_lock (spinlock_t)
 *   Protects: g_ext2_live (all fields), g_ext2_live_ready,
 *             ext2_alloc_block, ext2_free_block, ext2_sync_super_and_gdt,
 *             ext2_writeback_file, ext2_resize_file.
 *   Lock order: g_ext2_rw_lock -> (no inner locks held by ext2 code).
 *   Callers of ext2_alloc_block / ext2_free_block / ext2_trim_inode_blocks
 *   must already hold g_ext2_rw_lock (caller-holds convention).
 */
static ext2_mount_ctx_t g_ext2_live;
static int g_ext2_live_ready = 0;
static spinlock_t g_ext2_rw_lock;
static const ext2_fs_caps_t g_ext2_caps = {
    .write_in_place = 1,
    .write_extend = 1,
    .truncate = 1,
};

static uint64_t ext2_inode_size_bytes(const ext2_inode_t* ino)
{
    if (!ino) { return 0; }
    /*
     * size_high is valid for regular files in ext2 rev >= 1 with
     * RO_COMPAT_LARGE_FILE.  For directories the same field is dir_acl —
     * callers that traverse directory blocks only look at size_lo anyway,
     * and well-formed images keep dir_acl == 0.
     */
    uint64_t sz = (uint64_t)ino->size_lo | ((uint64_t)ino->size_high << 32);
    if (sz > (1ULL << 40)) {
        /* Sanity cap: 1 TB.  Likely a corrupted inode — ignore size_high. */
        kprintf("[EXT2] suspicious inode size 0x%llx, ignoring size_high\n",
                (unsigned long long)sz);
        return (uint64_t)ino->size_lo;
    }
    return sz;
}

static int ext2_is_dir(const ext2_inode_t* ino)
{
    return ino && ((ino->mode & 0xF000u) == EXT2_S_IFDIR);
}

static int ext2_is_reg(const ext2_inode_t* ino)
{
    return ino && ((ino->mode & 0xF000u) == EXT2_S_IFREG);
}

static void ext2_mark_node(vfs_node_t* node, uint32_t ino_num,
                           const ext2_inode_t* ext_ino)
{
    if (!node || !node->inode) {
        return;
    }
    node->inode->fs_tag = VFS_FS_TAG_EXT2;
    node->inode->fs_ino = (uint64_t)ino_num;
    if (ext_ino) {
        /* Copy on-disk DAC fields; strip file-type bits from mode. */
        node->inode->mode = (uint16_t)(ext_ino->mode & 0x0FFFu);
        node->inode->uid  = ext_ino->uid;
        node->inode->gid  = ext_ino->gid;
    }
}

static int ext2_read_bytes(ext2_mount_ctx_t* ctx, uint64_t offset, void* out, uint32_t len)
{
    if (!ctx || !ctx->bdev || !out || len == 0 || ctx->sector_size == 0) {
        return RDNX_E_INVALID;
    }

    uint8_t secbuf[512];
    if (ctx->sector_size > sizeof(secbuf)) {
        return RDNX_E_UNSUPPORTED;
    }

    uint8_t* dst = (uint8_t*)out;
    uint64_t cur = offset;
    uint32_t left = len;
    while (left > 0) {
        uint64_t sec = cur / ctx->sector_size;
        uint32_t sec_off = (uint32_t)(cur % ctx->sector_size);
        uint32_t chunk = ctx->sector_size - sec_off;
        if (chunk > left) {
            chunk = left;
        }

        int rc = fabric_blockdev_read(ctx->bdev, sec, 1, secbuf);
        if (rc != RDNX_OK) {
            return rc;
        }
        memcpy(dst, &secbuf[sec_off], chunk);
        dst += chunk;
        cur += chunk;
        left -= chunk;
    }
    return RDNX_OK;
}

static int ext2_write_bytes(ext2_mount_ctx_t* ctx, uint64_t offset, const void* in, uint32_t len)
{
    if (!ctx || !ctx->bdev || !in || len == 0 || ctx->sector_size == 0) {
        return RDNX_E_INVALID;
    }

    uint8_t secbuf[512];
    if (ctx->sector_size > sizeof(secbuf)) {
        return RDNX_E_UNSUPPORTED;
    }

    const uint8_t* src = (const uint8_t*)in;
    uint64_t cur = offset;
    uint32_t left = len;
    while (left > 0) {
        uint64_t sec = cur / ctx->sector_size;
        uint32_t sec_off = (uint32_t)(cur % ctx->sector_size);
        uint32_t chunk = ctx->sector_size - sec_off;
        if (chunk > left) {
            chunk = left;
        }

        if (chunk != ctx->sector_size) {
            int rrc = fabric_blockdev_read(ctx->bdev, sec, 1, secbuf);
            if (rrc != RDNX_OK) {
                return rrc;
            }
        } else {
            memset(secbuf, 0, sizeof(secbuf));
        }
        memcpy(&secbuf[sec_off], src, chunk);

        int wrc = fabric_blockdev_write(ctx->bdev, sec, 1, secbuf);
        if (wrc != RDNX_OK) {
            return wrc;
        }

        src += chunk;
        cur += chunk;
        left -= chunk;
    }
    return RDNX_OK;
}

static int ext2_read_block(ext2_mount_ctx_t* ctx, uint32_t block_no, void* out)
{
    if (!ctx || !out || ctx->block_size == 0) {
        return RDNX_E_INVALID;
    }
    uint64_t byte_off = (uint64_t)block_no * (uint64_t)ctx->block_size;
    return ext2_read_bytes(ctx, byte_off, out, ctx->block_size);
}

static int ext2_write_block(ext2_mount_ctx_t* ctx, uint32_t block_no, const void* in)
{
    if (!ctx || !in || ctx->block_size == 0) {
        return RDNX_E_INVALID;
    }
    uint64_t byte_off = (uint64_t)block_no * (uint64_t)ctx->block_size;
    return ext2_write_bytes(ctx, byte_off, in, ctx->block_size);
}

static int ext2_sync_super_and_gdt(ext2_mount_ctx_t* ctx)
{
    if (!ctx || !ctx->gdt || ctx->group_count == 0) {
        return RDNX_E_INVALID;
    }

    int rc = ext2_write_bytes(ctx, 1024u, &ctx->sb, sizeof(ctx->sb));
    if (rc != RDNX_OK) {
        return rc;
    }

    uint64_t gdt_off = (uint64_t)(ctx->sb.first_data_block + 1u) * ctx->block_size;
    uint32_t gdt_size = ctx->group_count * (uint32_t)sizeof(ext2_group_desc_t);
    return ext2_write_bytes(ctx, gdt_off, ctx->gdt, gdt_size);
}

static int ext2_read_inode(ext2_mount_ctx_t* ctx, uint32_t ino_num, ext2_inode_t* out)
{
    if (!ctx || !out || !ctx->gdt || ino_num == 0 || ctx->inode_size == 0) {
        return RDNX_E_INVALID;
    }

    uint32_t group = (ino_num - 1u) / ctx->sb.inodes_per_group;
    uint32_t index = (ino_num - 1u) % ctx->sb.inodes_per_group;
    if (group >= ctx->group_count) {
        return RDNX_E_INVALID;
    }

    uint32_t table_block = ctx->gdt[group].inode_table;
    if (table_block == 0) {
        return RDNX_E_INVALID;
    }

    uint64_t inode_off = ((uint64_t)table_block * ctx->block_size) + ((uint64_t)index * ctx->inode_size);
    uint8_t raw[EXT2_MAX_INODE_SIZE];
    if (ctx->inode_size > sizeof(raw)) {
        return RDNX_E_UNSUPPORTED;
    }

    int rc = ext2_read_bytes(ctx, inode_off, raw, ctx->inode_size);
    if (rc != RDNX_OK) {
        return rc;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out, raw, (ctx->inode_size < sizeof(*out)) ? ctx->inode_size : sizeof(*out));
    return RDNX_OK;
}

static int ext2_write_inode(ext2_mount_ctx_t* ctx, uint32_t ino_num, const ext2_inode_t* in)
{
    if (!ctx || !in || !ctx->gdt || ino_num == 0 || ctx->inode_size == 0) {
        return RDNX_E_INVALID;
    }

    uint32_t group = (ino_num - 1u) / ctx->sb.inodes_per_group;
    uint32_t index = (ino_num - 1u) % ctx->sb.inodes_per_group;
    if (group >= ctx->group_count) {
        return RDNX_E_INVALID;
    }

    uint32_t table_block = ctx->gdt[group].inode_table;
    if (table_block == 0) {
        return RDNX_E_INVALID;
    }

    uint64_t inode_off = ((uint64_t)table_block * ctx->block_size) + ((uint64_t)index * ctx->inode_size);
    uint8_t raw[EXT2_MAX_INODE_SIZE];
    if (ctx->inode_size > sizeof(raw)) {
        return RDNX_E_UNSUPPORTED;
    }

    int rc = ext2_read_bytes(ctx, inode_off, raw, ctx->inode_size);
    if (rc != RDNX_OK) {
        return rc;
    }
    memcpy(raw, in, (ctx->inode_size < sizeof(*in)) ? ctx->inode_size : sizeof(*in));
    return ext2_write_bytes(ctx, inode_off, raw, ctx->inode_size);
}

static int ext2_alloc_block(ext2_mount_ctx_t* ctx, uint32_t* out_blk)
{
    if (!ctx || !ctx->gdt || !out_blk || ctx->block_size == 0) {
        return RDNX_E_INVALID;
    }
    if (ctx->sb.free_blocks_count == 0) {
        return RDNX_E_GENERIC;
    }

    uint8_t* bmap = (uint8_t*)kmalloc(ctx->block_size);
    if (!bmap) {
        return RDNX_E_NOMEM;
    }

    uint32_t total_data_blocks = ctx->sb.blocks_count - ctx->sb.first_data_block;
    for (uint32_t g = 0; g < ctx->group_count; g++) {
        if (ctx->gdt[g].free_blocks_count == 0 || ctx->gdt[g].block_bitmap == 0) {
            continue;
        }

        uint32_t gbase = g * ctx->sb.blocks_per_group;
        if (gbase >= total_data_blocks) {
            continue;
        }
        uint32_t group_blocks = total_data_blocks - gbase;
        if (group_blocks > ctx->sb.blocks_per_group) {
            group_blocks = ctx->sb.blocks_per_group;
        }

        int rc = ext2_read_block(ctx, ctx->gdt[g].block_bitmap, bmap);
        if (rc != RDNX_OK) {
            kfree(bmap);
            return rc;
        }

        for (uint32_t bi = 0; bi < group_blocks; bi++) {
            uint32_t byte = bi >> 3;
            uint8_t mask = (uint8_t)(1u << (bi & 7u));
            if ((bmap[byte] & mask) != 0) {
                continue;
            }

            bmap[byte] |= mask;
            rc = ext2_write_block(ctx, ctx->gdt[g].block_bitmap, bmap);
            if (rc != RDNX_OK) {
                kfree(bmap);
                return rc;
            }

            if (ctx->gdt[g].free_blocks_count > 0) {
                ctx->gdt[g].free_blocks_count--;
            }
            if (ctx->sb.free_blocks_count > 0) {
                ctx->sb.free_blocks_count--;
            }
            rc = ext2_sync_super_and_gdt(ctx);
            if (rc != RDNX_OK) {
                kfree(bmap);
                return rc;
            }

            *out_blk = ctx->sb.first_data_block + gbase + bi;
            kfree(bmap);
            return RDNX_OK;
        }
    }

    kfree(bmap);
    return RDNX_E_GENERIC;
}

static int ext2_free_block(ext2_mount_ctx_t* ctx, uint32_t blk)
{
    if (!ctx || !ctx->gdt || ctx->block_size == 0) {
        return RDNX_E_INVALID;
    }
    if (blk < ctx->sb.first_data_block || blk >= ctx->sb.blocks_count) {
        return RDNX_E_INVALID;
    }

    uint32_t rel = blk - ctx->sb.first_data_block;
    uint32_t total_data_blocks = ctx->sb.blocks_count - ctx->sb.first_data_block;
    if (rel >= total_data_blocks || ctx->sb.blocks_per_group == 0) {
        return RDNX_E_INVALID;
    }

    uint32_t g = rel / ctx->sb.blocks_per_group;
    uint32_t bi = rel % ctx->sb.blocks_per_group;
    if (g >= ctx->group_count || ctx->gdt[g].block_bitmap == 0) {
        return RDNX_E_INVALID;
    }

    uint8_t* bmap = (uint8_t*)kmalloc(ctx->block_size);
    if (!bmap) {
        return RDNX_E_NOMEM;
    }

    int rc = ext2_read_block(ctx, ctx->gdt[g].block_bitmap, bmap);
    if (rc != RDNX_OK) {
        kfree(bmap);
        return rc;
    }

    uint32_t byte = bi >> 3;
    uint8_t mask = (uint8_t)(1u << (bi & 7u));
    if ((bmap[byte] & mask) == 0) {
        kfree(bmap);
        return RDNX_OK;
    }
    bmap[byte] &= (uint8_t)~mask;

    rc = ext2_write_block(ctx, ctx->gdt[g].block_bitmap, bmap);
    kfree(bmap);
    if (rc != RDNX_OK) {
        return rc;
    }

    if (ctx->gdt[g].free_blocks_count != UINT16_MAX) {
        ctx->gdt[g].free_blocks_count++;
    }
    if (ctx->sb.free_blocks_count != UINT32_MAX) {
        ctx->sb.free_blocks_count++;
    }
    return ext2_sync_super_and_gdt(ctx);
}

static int ext2_zero_block(ext2_mount_ctx_t* ctx, uint32_t block_no)
{
    if (!ctx || block_no == 0) {
        return RDNX_E_INVALID;
    }
    uint8_t* zero = (uint8_t*)kmalloc(ctx->block_size);
    if (!zero) {
        return RDNX_E_NOMEM;
    }
    memset(zero, 0, ctx->block_size);
    int rc = ext2_write_block(ctx, block_no, zero);
    kfree(zero);
    return rc;
}

static int ext2_inode_get_block(ext2_mount_ctx_t* ctx,
                                const ext2_inode_t* ino,
                                uint32_t lbn,
                                uint32_t* out_blk)
{
    if (!ctx || !ino || !out_blk) {
        return RDNX_E_INVALID;
    }
    if (lbn < EXT2_NDIR_BLOCKS) {
        *out_blk = ino->block[lbn];
        return RDNX_OK;
    }

    uint32_t per_block = ctx->block_size / sizeof(uint32_t);
    uint32_t idx = lbn - EXT2_NDIR_BLOCKS;

    /* Single indirect: block[12] */
    if (idx < per_block) {
        if (ino->block[12] == 0) {
            *out_blk = 0;
            return RDNX_OK;
        }
        uint8_t* ibuf = (uint8_t*)kmalloc(ctx->block_size);
        if (!ibuf) {
            return RDNX_E_NOMEM;
        }
        int rc = ext2_read_block(ctx, ino->block[12], ibuf);
        if (rc == RDNX_OK) {
            *out_blk = ((const uint32_t*)ibuf)[idx];
        }
        kfree(ibuf);
        return rc;
    }

    /* Double indirect: block[13] */
    idx -= per_block;
    if (idx < per_block * per_block) {
        if (ino->block[13] == 0) {
            *out_blk = 0;
            return RDNX_OK;
        }
        uint8_t* ibuf = (uint8_t*)kmalloc(ctx->block_size);
        if (!ibuf) {
            return RDNX_E_NOMEM;
        }
        int rc = ext2_read_block(ctx, ino->block[13], ibuf);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
        uint32_t di_blk = ((const uint32_t*)ibuf)[idx / per_block];
        if (di_blk == 0) {
            kfree(ibuf);
            *out_blk = 0;
            return RDNX_OK;
        }
        rc = ext2_read_block(ctx, di_blk, ibuf);
        if (rc == RDNX_OK) {
            *out_blk = ((const uint32_t*)ibuf)[idx % per_block];
        }
        kfree(ibuf);
        return rc;
    }

    return RDNX_E_UNSUPPORTED;
}

static int ext2_inode_get_or_alloc_block(ext2_mount_ctx_t* ctx,
                                         uint32_t ino_num,
                                         ext2_inode_t* ino,
                                         uint32_t lbn,
                                         int alloc,
                                         uint32_t* out_blk,
                                         int* out_inode_dirty)
{
    if (!ctx || !ino || !out_blk || !out_inode_dirty) {
        return RDNX_E_INVALID;
    }
    *out_blk = 0;

    uint32_t sectors_per_block = ctx->block_size / 512u;
    if (sectors_per_block == 0) {
        return RDNX_E_UNSUPPORTED;
    }

    if (lbn < EXT2_NDIR_BLOCKS) {
        uint32_t blk = ino->block[lbn];
        if (blk == 0 && alloc) {
            int rc = ext2_alloc_block(ctx, &blk);
            if (rc != RDNX_OK) {
                return rc;
            }
            rc = ext2_zero_block(ctx, blk);
            if (rc != RDNX_OK) {
                return rc;
            }
            ino->block[lbn] = blk;
            ino->blocks += sectors_per_block;
            *out_inode_dirty = 1;
            rc = ext2_write_inode(ctx, ino_num, ino);
            if (rc != RDNX_OK) {
                return rc;
            }
        }
        *out_blk = blk;
        return RDNX_OK;
    }

    uint32_t per_block = ctx->block_size / sizeof(uint32_t);
    uint32_t idx = lbn - EXT2_NDIR_BLOCKS;

    /* Single indirect: block[12] */
    if (idx < per_block) {
        if (ino->block[12] == 0) {
            if (!alloc) {
                return RDNX_OK;
            }
            uint32_t ind_blk = 0;
            int rc = ext2_alloc_block(ctx, &ind_blk);
            if (rc != RDNX_OK) {
                return rc;
            }
            rc = ext2_zero_block(ctx, ind_blk);
            if (rc != RDNX_OK) {
                return rc;
            }
            ino->block[12] = ind_blk;
            ino->blocks += sectors_per_block;
            *out_inode_dirty = 1;
            rc = ext2_write_inode(ctx, ino_num, ino);
            if (rc != RDNX_OK) {
                return rc;
            }
        }

        uint8_t* ibuf = (uint8_t*)kmalloc(ctx->block_size);
        if (!ibuf) {
            return RDNX_E_NOMEM;
        }
        int rc = ext2_read_block(ctx, ino->block[12], ibuf);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
        uint32_t* table = (uint32_t*)ibuf;
        uint32_t blk = table[idx];
        if (blk == 0 && alloc) {
            rc = ext2_alloc_block(ctx, &blk);
            if (rc != RDNX_OK) {
                kfree(ibuf);
                return rc;
            }
            rc = ext2_zero_block(ctx, blk);
            if (rc != RDNX_OK) {
                kfree(ibuf);
                return rc;
            }
            table[idx] = blk;
            rc = ext2_write_block(ctx, ino->block[12], ibuf);
            if (rc != RDNX_OK) {
                kfree(ibuf);
                return rc;
            }
            ino->blocks += sectors_per_block;
            *out_inode_dirty = 1;
            rc = ext2_write_inode(ctx, ino_num, ino);
            if (rc != RDNX_OK) {
                kfree(ibuf);
                return rc;
            }
        }
        *out_blk = blk;
        kfree(ibuf);
        return RDNX_OK;
    }

    /* Double indirect: block[13] */
    uint32_t idx2  = idx - per_block;
    uint32_t di_idx = idx2 / per_block;
    uint32_t si_idx = idx2 % per_block;
    if (idx2 >= per_block * per_block) {
        return RDNX_E_UNSUPPORTED;
    }

    /* Ensure double indirect table block exists */
    if (ino->block[13] == 0) {
        if (!alloc) {
            return RDNX_OK;
        }
        uint32_t dind_blk = 0;
        int rc = ext2_alloc_block(ctx, &dind_blk);
        if (rc != RDNX_OK) {
            return rc;
        }
        rc = ext2_zero_block(ctx, dind_blk);
        if (rc != RDNX_OK) {
            return rc;
        }
        ino->block[13] = dind_blk;
        ino->blocks += sectors_per_block;
        *out_inode_dirty = 1;
        rc = ext2_write_inode(ctx, ino_num, ino);
        if (rc != RDNX_OK) {
            return rc;
        }
    }

    uint8_t* ibuf = (uint8_t*)kmalloc(ctx->block_size);
    if (!ibuf) {
        return RDNX_E_NOMEM;
    }

    /* Read double indirect table */
    int rc = ext2_read_block(ctx, ino->block[13], ibuf);
    if (rc != RDNX_OK) {
        kfree(ibuf);
        return rc;
    }
    uint32_t* di_table = (uint32_t*)ibuf;
    uint32_t si_blk = di_table[di_idx];

    /* Ensure single indirect sub-block exists */
    if (si_blk == 0) {
        if (!alloc) {
            kfree(ibuf);
            return RDNX_OK;
        }
        rc = ext2_alloc_block(ctx, &si_blk);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
        rc = ext2_zero_block(ctx, si_blk);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
        di_table[di_idx] = si_blk;
        rc = ext2_write_block(ctx, ino->block[13], ibuf);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
        ino->blocks += sectors_per_block;
        *out_inode_dirty = 1;
        rc = ext2_write_inode(ctx, ino_num, ino);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
    }

    /* Read single indirect sub-table */
    rc = ext2_read_block(ctx, si_blk, ibuf);
    if (rc != RDNX_OK) {
        kfree(ibuf);
        return rc;
    }
    uint32_t* si_table = (uint32_t*)ibuf;
    uint32_t blk = si_table[si_idx];

    if (blk == 0 && alloc) {
        rc = ext2_alloc_block(ctx, &blk);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
        rc = ext2_zero_block(ctx, blk);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
        si_table[si_idx] = blk;
        rc = ext2_write_block(ctx, si_blk, ibuf);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
        ino->blocks += sectors_per_block;
        *out_inode_dirty = 1;
        rc = ext2_write_inode(ctx, ino_num, ino);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }
    }

    *out_blk = blk;
    kfree(ibuf);
    return RDNX_OK;
}

static uint32_t ext2_count_allocated_blocks(ext2_mount_ctx_t* ctx, const ext2_inode_t* ino)
{
    if (!ctx || !ino) {
        return 0;
    }

    uint32_t total = 0;
    for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (ino->block[i] != 0) {
            total++;
        }
    }

    uint32_t per_block = ctx->block_size / sizeof(uint32_t);

    if (ino->block[12] != 0) {
        total++; /* single indirect block itself */
        uint8_t* ibuf = (uint8_t*)kmalloc(ctx->block_size);
        if (!ibuf) {
            return total;
        }
        if (ext2_read_block(ctx, ino->block[12], ibuf) == RDNX_OK) {
            const uint32_t* table = (const uint32_t*)ibuf;
            for (uint32_t i = 0; i < per_block; i++) {
                if (table[i] != 0) {
                    total++;
                }
            }
        }
        kfree(ibuf);
    }

    if (ino->block[13] != 0) {
        total++; /* double indirect table itself */
        uint8_t* di_buf = (uint8_t*)kmalloc(ctx->block_size);
        if (!di_buf) {
            return total;
        }
        if (ext2_read_block(ctx, ino->block[13], di_buf) == RDNX_OK) {
            const uint32_t* di_table = (const uint32_t*)di_buf;
            uint8_t* si_buf = (uint8_t*)kmalloc(ctx->block_size);
            if (si_buf) {
                for (uint32_t i = 0; i < per_block; i++) {
                    if (di_table[i] == 0) {
                        continue;
                    }
                    total++; /* single indirect sub-block */
                    if (ext2_read_block(ctx, di_table[i], si_buf) == RDNX_OK) {
                        const uint32_t* si_table = (const uint32_t*)si_buf;
                        for (uint32_t j = 0; j < per_block; j++) {
                            if (si_table[j] != 0) {
                                total++;
                            }
                        }
                    }
                }
                kfree(si_buf);
            }
        }
        kfree(di_buf);
    }

    return total;
}

static int ext2_trim_inode_blocks(ext2_mount_ctx_t* ctx, uint32_t ino_num, ext2_inode_t* ino, uint32_t target_blocks)
{
    if (!ctx || !ino) {
        return RDNX_E_INVALID;
    }

    uint32_t per_block = ctx->block_size / sizeof(uint32_t);
    uint32_t direct_keep = (target_blocks < EXT2_NDIR_BLOCKS) ? target_blocks : EXT2_NDIR_BLOCKS;

    for (int i = (int)EXT2_NDIR_BLOCKS - 1; i >= 0; i--) {
        if ((uint32_t)i < direct_keep) {
            continue;
        }
        uint32_t blk = ino->block[(uint32_t)i];
        if (blk != 0) {
            int frc = ext2_free_block(ctx, blk);
            if (frc != RDNX_OK) {
                return frc;
            }
            ino->block[(uint32_t)i] = 0;
        }
    }

    if (ino->block[12] != 0) {
        uint8_t* ibuf = (uint8_t*)kmalloc(ctx->block_size);
        if (!ibuf) {
            return RDNX_E_NOMEM;
        }
        int rc = ext2_read_block(ctx, ino->block[12], ibuf);
        if (rc != RDNX_OK) {
            kfree(ibuf);
            return rc;
        }

        uint32_t* table = (uint32_t*)ibuf;
        uint32_t indir_keep = (target_blocks > EXT2_NDIR_BLOCKS) ? (target_blocks - EXT2_NDIR_BLOCKS) : 0;
        if (indir_keep > per_block) {
            indir_keep = per_block;
        }
        int dirty = 0;
        for (int i = (int)per_block - 1; i >= 0; i--) {
            if ((uint32_t)i < indir_keep) {
                continue;
            }
            uint32_t blk = table[(uint32_t)i];
            if (blk != 0) {
                int frc = ext2_free_block(ctx, blk);
                if (frc != RDNX_OK) {
                    kfree(ibuf);
                    return frc;
                }
                table[(uint32_t)i] = 0;
                dirty = 1;
            }
        }

        int any = 0;
        for (uint32_t i = 0; i < per_block; i++) {
            if (table[i] != 0) {
                any = 1;
                break;
            }
        }
        if (!any) {
            uint32_t ind = ino->block[12];
            ino->block[12] = 0;
            rc = ext2_free_block(ctx, ind);
            if (rc != RDNX_OK) {
                kfree(ibuf);
                return rc;
            }
        } else if (dirty) {
            rc = ext2_write_block(ctx, ino->block[12], ibuf);
            if (rc != RDNX_OK) {
                kfree(ibuf);
                return rc;
            }
        }
        kfree(ibuf);
    }

    /* Double indirect: block[13] */
    if (ino->block[13] != 0) {
        uint32_t di_data_keep = (target_blocks > EXT2_NDIR_BLOCKS + per_block)
                                ? (target_blocks - EXT2_NDIR_BLOCKS - per_block) : 0;

        uint8_t* di_buf = (uint8_t*)kmalloc(ctx->block_size);
        if (!di_buf) {
            return RDNX_E_NOMEM;
        }
        int rc = ext2_read_block(ctx, ino->block[13], di_buf);
        if (rc != RDNX_OK) {
            kfree(di_buf);
            return rc;
        }
        uint32_t* di_table = (uint32_t*)di_buf;
        int di_dirty = 0;

        uint8_t* si_buf = (uint8_t*)kmalloc(ctx->block_size);
        if (!si_buf) {
            kfree(di_buf);
            return RDNX_E_NOMEM;
        }

        for (int di_i = (int)per_block - 1; di_i >= 0; di_i--) {
            uint32_t si_blk = di_table[(uint32_t)di_i];
            if (si_blk == 0) {
                continue;
            }
            uint32_t di_base = (uint32_t)di_i * per_block;

            if (di_data_keep <= di_base) {
                /* Free entire sub-table */
                if (ext2_read_block(ctx, si_blk, si_buf) == RDNX_OK) {
                    uint32_t* si_table = (uint32_t*)si_buf;
                    for (uint32_t si_i = 0; si_i < per_block; si_i++) {
                        if (si_table[si_i] != 0) {
                            int frc = ext2_free_block(ctx, si_table[si_i]);
                            if (frc != RDNX_OK) {
                                kfree(si_buf);
                                kfree(di_buf);
                                return frc;
                            }
                            si_table[si_i] = 0;
                        }
                    }
                }
                int frc = ext2_free_block(ctx, si_blk);
                if (frc != RDNX_OK) {
                    kfree(si_buf);
                    kfree(di_buf);
                    return frc;
                }
                di_table[(uint32_t)di_i] = 0;
                di_dirty = 1;
            } else if (di_data_keep < di_base + per_block) {
                /* Partial trim of sub-table */
                uint32_t si_keep = di_data_keep - di_base;
                rc = ext2_read_block(ctx, si_blk, si_buf);
                if (rc != RDNX_OK) {
                    kfree(si_buf);
                    kfree(di_buf);
                    return rc;
                }
                uint32_t* si_table = (uint32_t*)si_buf;
                int si_dirty = 0;
                for (int si_i = (int)per_block - 1; si_i >= 0; si_i--) {
                    if ((uint32_t)si_i < si_keep) {
                        continue;
                    }
                    if (si_table[(uint32_t)si_i] != 0) {
                        int frc = ext2_free_block(ctx, si_table[(uint32_t)si_i]);
                        if (frc != RDNX_OK) {
                            kfree(si_buf);
                            kfree(di_buf);
                            return frc;
                        }
                        si_table[(uint32_t)si_i] = 0;
                        si_dirty = 1;
                    }
                }
                if (si_dirty) {
                    int wrc = ext2_write_block(ctx, si_blk, si_buf);
                    if (wrc != RDNX_OK) {
                        kfree(si_buf);
                        kfree(di_buf);
                        return wrc;
                    }
                }
            }
            /* else: keep entire sub-table */
        }

        kfree(si_buf);

        /* Free double indirect table if now empty */
        int any = 0;
        for (uint32_t i = 0; i < per_block; i++) {
            if (di_table[i] != 0) {
                any = 1;
                break;
            }
        }
        if (!any) {
            uint32_t dind = ino->block[13];
            ino->block[13] = 0;
            int frc = ext2_free_block(ctx, dind);
            if (frc != RDNX_OK) {
                kfree(di_buf);
                return frc;
            }
        } else if (di_dirty) {
            int wrc = ext2_write_block(ctx, ino->block[13], di_buf);
            if (wrc != RDNX_OK) {
                kfree(di_buf);
                return wrc;
            }
        }
        kfree(di_buf);
    }

    uint32_t sectors_per_block = ctx->block_size / 512u;
    uint32_t alloc_blocks = ext2_count_allocated_blocks(ctx, ino);
    ino->blocks = alloc_blocks * sectors_per_block;
    return ext2_write_inode(ctx, ino_num, ino);
}

/* -------------------------------------------------------------------------
 * Demand-paging / lazy inode support
 * ---------------------------------------------------------------------- */

typedef struct {
    uint32_t ino_num;
} ext2_pager_ctx_t;

/**
 * Read an arbitrary byte range from an ext2 file directly from disk.
 * Used for lazy vfs_read() and as a building block for ext2_pager_read_page().
 * Returns number of bytes read (>= 0) or negative error code.
 */
int ext2_read_file_range(uint64_t ino_num, uint64_t offset, void* buf, size_t len)
{
    if (!buf || len == 0) {
        return 0;
    }
    ext2_inode_t ino;
    if (ext2_read_inode(&g_ext2_live, (uint32_t)ino_num, &ino) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    uint64_t fsize = ext2_inode_size_bytes(&ino);
    if (offset >= fsize) {
        return 0;
    }
    if (offset + (uint64_t)len > fsize) {
        len = (size_t)(fsize - offset);
    }

    uint8_t* blk_buf = (uint8_t*)kmalloc(g_ext2_live.block_size);
    if (!blk_buf) {
        return RDNX_E_NOMEM;
    }

    uint8_t* dst = (uint8_t*)buf;
    size_t done = 0;
    while (done < len) {
        uint64_t abs_off = offset + (uint64_t)done;
        uint32_t lbn     = (uint32_t)(abs_off / (uint64_t)g_ext2_live.block_size);
        uint32_t in_blk  = (uint32_t)(abs_off % (uint64_t)g_ext2_live.block_size);

        uint32_t pblk = 0;
        if (ext2_inode_get_block(&g_ext2_live, &ino, lbn, &pblk) != RDNX_OK) {
            break;
        }
        if (pblk == 0) {
            memset(blk_buf, 0, g_ext2_live.block_size);
        } else if (ext2_read_block(&g_ext2_live, pblk, blk_buf) != RDNX_OK) {
            break;
        }

        size_t chunk = (size_t)g_ext2_live.block_size - (size_t)in_blk;
        if (chunk > len - done) {
            chunk = len - done;
        }
        memcpy(dst + done, blk_buf + in_blk, chunk);
        done += chunk;
    }

    kfree(blk_buf);
    return (int)done;
}

/**
 * vm_file_backing read_page callback — fills one VM_OBJECT_PAGE_SIZE page
 * from an ext2 file identified by ino_num in pager_priv.
 */
static int ext2_pager_read_page(void* pager_priv, uint64_t page_off, void* page_buf)
{
    ext2_pager_ctx_t* pctx = (ext2_pager_ctx_t*)pager_priv;
    memset(page_buf, 0, 4096u);
    int n = ext2_read_file_range((uint64_t)pctx->ino_num, page_off, page_buf, 4096u);
    return (n >= 0) ? RDNX_OK : n;
}

/**
 * Allocate a vm_file_backing_t wired to an ext2 inode for demand-paged mmap.
 * Caller transfers ownership to a vm_object (which will free it on unref).
 */
#include "../vm/vm_object.h"
vm_file_backing_t* ext2_file_backing_create(vfs_inode_t* inode)
{
    if (!inode || inode->fs_tag != VFS_FS_TAG_EXT2) {
        return NULL;
    }
    ext2_pager_ctx_t* pctx = (ext2_pager_ctx_t*)kmalloc(sizeof(ext2_pager_ctx_t));
    if (!pctx) {
        return NULL;
    }
    pctx->ino_num = (uint32_t)inode->fs_ino;

    vm_file_backing_t* fb = (vm_file_backing_t*)kmalloc(sizeof(vm_file_backing_t));
    if (!fb) {
        kfree(pctx);
        return NULL;
    }
    fb->data        = NULL;
    fb->size        = (uint64_t)inode->size;
    fb->file_offset = 0;
    fb->read_page   = ext2_pager_read_page;
    fb->pager_priv  = pctx;
    return fb;
}

/**
 * Lazy inode init — records size/ino, leaves data = NULL so reads go
 * through ext2_read_file_range() and mmaps use demand paging.
 */
static int ext2_init_lazy_inode(uint32_t ino_num, const ext2_inode_t* ino, vfs_node_t* node)
{
    if (!ino || !node || !node->inode) {
        return RDNX_E_INVALID;
    }
    node->inode->size     = (size_t)ext2_inode_size_bytes(ino);
    node->inode->capacity = 0;
    node->inode->data     = NULL;
    ext2_mark_node(node, ino_num, ino);
    return RDNX_OK;
}

static int ext2_build_dir(ext2_mount_ctx_t* ctx,
                          vfs_node_t* parent,
                          uint32_t dir_ino_num,
                          const ext2_inode_t* dir_ino,
                          uint32_t depth)
{
    if (!ctx || !parent || !dir_ino || !ext2_is_dir(dir_ino)) {
        return RDNX_E_INVALID;
    }
    if (depth > EXT2_MAX_TREE_DEPTH) {
        return RDNX_OK;
    }

    uint8_t* blk = (uint8_t*)kmalloc(ctx->block_size);
    if (!blk) {
        return RDNX_E_NOMEM;
    }

    uint64_t dir_size = ext2_inode_size_bytes(dir_ino);
    uint32_t scanned = 0;
    uint32_t lbn = 0;
    while (scanned < dir_size) {
        uint32_t pblk = 0;
        int rc = ext2_inode_get_block(ctx, dir_ino, lbn, &pblk);
        if (rc != RDNX_OK) {
            kfree(blk);
            return rc;
        }
        if (pblk == 0) {
            break;
        }
        rc = ext2_read_block(ctx, pblk, blk);
        if (rc != RDNX_OK) {
            kfree(blk);
            return rc;
        }

        uint32_t pos = 0;
        while (pos + sizeof(ext2_dirent_hdr_t) <= ctx->block_size) {
            const ext2_dirent_hdr_t* de = (const ext2_dirent_hdr_t*)&blk[pos];
            if (de->rec_len < sizeof(ext2_dirent_hdr_t) || pos + de->rec_len > ctx->block_size) {
                break;
            }
            if (de->inode != 0 && de->name_len > 0) {
                const uint8_t* name_raw = &blk[pos + sizeof(ext2_dirent_hdr_t)];
                uint8_t max_name = (uint8_t)(de->rec_len - sizeof(ext2_dirent_hdr_t));
                if (de->name_len <= max_name) {
                    char name[32];
                    uint32_t n = de->name_len;
                    if (n >= sizeof(name)) {
                        n = sizeof(name) - 1u;
                    }
                    memcpy(name, name_raw, n);
                    name[n] = '\0';

                    if (!((n == 1 && name[0] == '.') || (n == 2 && name[0] == '.' && name[1] == '.'))) {
                        if (ctx->node_budget == 0) {
                            kfree(blk);
                            return RDNX_OK;
                        }

                        ext2_inode_t child_ino;
                        if (ext2_read_inode(ctx, de->inode, &child_ino) == RDNX_OK) {
                            vfs_node_type_t nt = ext2_is_dir(&child_ino) ? VFS_NODE_DIR : VFS_NODE_FILE;
                            vfs_node_t* child = vfs_fs_alloc_node(name, nt);
                            if (child) {
                                if (vfs_fs_add_child(parent, child) == RDNX_OK) {
                                    ext2_mark_node(child, de->inode, &child_ino);
                                    ctx->node_budget--;
                                    if (nt == VFS_NODE_DIR) {
                                        (void)ext2_build_dir(ctx, child, de->inode, &child_ino, depth + 1u);
                                    } else if (ext2_is_reg(&child_ino)) {
                                        (void)ext2_init_lazy_inode(de->inode, &child_ino, child);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            pos += de->rec_len;
        }

        scanned += ctx->block_size;
        lbn++;
        (void)dir_ino_num;
    }

    kfree(blk);
    return RDNX_OK;
}

int ext2_writeback_file(vfs_node_t* node, size_t off, const void* data, size_t len, size_t final_size)
{
    spinlock_lock(&g_ext2_rw_lock);
    if (!node || !node->inode || !data) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_INVALID;
    }
    if (!g_ext2_live_ready || !g_ext2_live.bdev || !g_ext2_live.gdt) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_UNSUPPORTED;
    }
    if (node->inode->fs_tag != VFS_FS_TAG_EXT2 || node->inode->fs_ino == 0) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_UNSUPPORTED;
    }
    if (len == 0) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_OK;
    }

    ext2_inode_t ino;
    int irc = ext2_read_inode(&g_ext2_live, (uint32_t)node->inode->fs_ino, &ino);
    if (irc != RDNX_OK) {
        spinlock_unlock(&g_ext2_rw_lock);
        return irc;
    }
    if (!ext2_is_reg(&ino)) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_UNSUPPORTED;
    }

    uint64_t disk_size = ext2_inode_size_bytes(&ino);
    if ((uint64_t)off + (uint64_t)len > (uint64_t)final_size) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_UNSUPPORTED;
    }

    uint8_t* blk = (uint8_t*)kmalloc(g_ext2_live.block_size);
    if (!blk) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_NOMEM;
    }

    const uint8_t* src = (const uint8_t*)data;
    size_t done = 0;
    int inode_dirty = 0;
    while (done < len) {
        uint64_t abs = (uint64_t)off + (uint64_t)done;
        uint32_t lbn = (uint32_t)(abs / g_ext2_live.block_size);
        uint32_t boff = (uint32_t)(abs % g_ext2_live.block_size);
        size_t chunk = len - done;
        size_t room = (size_t)g_ext2_live.block_size - (size_t)boff;
        if (chunk > room) {
            chunk = room;
        }

        uint32_t pblk = 0;
        int brc = ext2_inode_get_or_alloc_block(&g_ext2_live,
                                                (uint32_t)node->inode->fs_ino,
                                                &ino, lbn, 1, &pblk, &inode_dirty);
        if (brc != RDNX_OK || pblk == 0) {
            kfree(blk);
            spinlock_unlock(&g_ext2_rw_lock);
            return RDNX_E_UNSUPPORTED;
        }

        if (chunk == g_ext2_live.block_size && boff == 0) {
            memset(blk, 0, g_ext2_live.block_size);
        } else {
            brc = ext2_read_block(&g_ext2_live, pblk, blk);
            if (brc != RDNX_OK) {
                kfree(blk);
                spinlock_unlock(&g_ext2_rw_lock);
                return brc;
            }
        }
        memcpy(blk + boff, src + done, chunk);
        brc = ext2_write_bytes(&g_ext2_live, (uint64_t)pblk * (uint64_t)g_ext2_live.block_size,
                               blk, g_ext2_live.block_size);
        if (brc != RDNX_OK) {
            kfree(blk);
            spinlock_unlock(&g_ext2_rw_lock);
            return brc;
        }
        done += chunk;
    }

    if ((uint64_t)final_size > disk_size) {
        ino.size_lo   = (uint32_t)(final_size & 0xFFFFFFFFu);
        ino.size_high = (uint32_t)(final_size >> 32);
        inode_dirty = 1;
    }
    if (inode_dirty) {
        int wrc = ext2_write_inode(&g_ext2_live, (uint32_t)node->inode->fs_ino, &ino);
        if (wrc != RDNX_OK) {
            kfree(blk);
            spinlock_unlock(&g_ext2_rw_lock);
            return wrc;
        }
    }

    kfree(blk);
    spinlock_unlock(&g_ext2_rw_lock);
    return RDNX_OK;
}

int ext2_query_caps(ext2_fs_caps_t* out_caps)
{
    if (!out_caps) {
        return RDNX_E_INVALID;
    }
    *out_caps = g_ext2_caps;
    return RDNX_OK;
}

int ext2_resize_file(vfs_node_t* node, size_t new_size)
{
    spinlock_lock(&g_ext2_rw_lock);
    if (!node || !node->inode) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_INVALID;
    }
    if (!g_ext2_live_ready || !g_ext2_live.bdev || !g_ext2_live.gdt) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_UNSUPPORTED;
    }
    if (node->inode->fs_tag != VFS_FS_TAG_EXT2 || node->inode->fs_ino == 0) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_UNSUPPORTED;
    }

    ext2_inode_t ino;
    int irc = ext2_read_inode(&g_ext2_live, (uint32_t)node->inode->fs_ino, &ino);
    if (irc != RDNX_OK) {
        spinlock_unlock(&g_ext2_rw_lock);
        return irc;
    }
    if (!ext2_is_reg(&ino)) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_E_UNSUPPORTED;
    }

    uint64_t disk_size = ext2_inode_size_bytes(&ino);
    if ((uint64_t)new_size == disk_size) {
        spinlock_unlock(&g_ext2_rw_lock);
        return RDNX_OK;
    }

    uint32_t old_blocks = (uint32_t)((disk_size + g_ext2_live.block_size - 1u) / g_ext2_live.block_size);
    uint32_t new_blocks = (uint32_t)(((uint64_t)new_size + g_ext2_live.block_size - 1u) / g_ext2_live.block_size);

    if ((uint64_t)new_size > disk_size) {
        int inode_dirty = 0;
        for (uint32_t lbn = old_blocks; lbn < new_blocks; lbn++) {
            uint32_t pblk = 0;
            int rc = ext2_inode_get_or_alloc_block(&g_ext2_live,
                                                   (uint32_t)node->inode->fs_ino,
                                                   &ino, lbn, 1, &pblk, &inode_dirty);
            if (rc != RDNX_OK || pblk == 0) {
                int trim_rc = ext2_trim_inode_blocks(&g_ext2_live,
                    (uint32_t)node->inode->fs_ino, &ino, old_blocks);
                if (trim_rc != RDNX_OK) {
                    kprintf("[EXT2] resize rollback failed: alloc_err=%d trim_err=%d ino=%u\n",
                            rc, trim_rc, (uint32_t)node->inode->fs_ino);
                }
                spinlock_unlock(&g_ext2_rw_lock);
                return (rc != RDNX_OK) ? rc : RDNX_E_UNSUPPORTED;
            }
        }

        ino.size_lo   = (uint32_t)(new_size & 0xFFFFFFFFu);
        ino.size_high = (uint32_t)(new_size >> 32);
        uint32_t sectors_per_block = g_ext2_live.block_size / 512u;
        uint32_t alloc_blocks = ext2_count_allocated_blocks(&g_ext2_live, &ino);
        ino.blocks = alloc_blocks * sectors_per_block;
        int rc = ext2_write_inode(&g_ext2_live, (uint32_t)node->inode->fs_ino, &ino);
        spinlock_unlock(&g_ext2_rw_lock);
        return rc;
    } else {
        /* Crash-safer shrink: size first, then free tail blocks. */
        ino.size_lo   = (uint32_t)(new_size & 0xFFFFFFFFu);
        ino.size_high = (uint32_t)(new_size >> 32);
        int rc = ext2_write_inode(&g_ext2_live, (uint32_t)node->inode->fs_ino, &ino);
        if (rc != RDNX_OK) {
            spinlock_unlock(&g_ext2_rw_lock);
            return rc;
        }
        rc = ext2_trim_inode_blocks(&g_ext2_live, (uint32_t)node->inode->fs_ino, &ino, new_blocks);
        spinlock_unlock(&g_ext2_rw_lock);
        return rc;
    }
}

static int ext2_mount(const char* source, vfs_node_t** out_root)
{
    const char* disk_name = (source && source[0]) ? source : "disk0";
    if (!out_root) {
        return RDNX_E_INVALID;
    }

    ext2_mount_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.bdev = fabric_blockdev_find(disk_name);
    if (!ctx.bdev) {
        return RDNX_E_NOTFOUND;
    }

    ctx.sector_size = ctx.bdev->sector_size;
    if (ctx.sector_size == 0 || ctx.sector_size > 512u) {
        return RDNX_E_UNSUPPORTED;
    }

    int rc = ext2_read_bytes(&ctx, 1024u, &ctx.sb, sizeof(ctx.sb));
    if (rc != RDNX_OK) {
        return rc;
    }
    if (ctx.sb.magic != EXT2_MAGIC) {
        return RDNX_E_INVALID;
    }
    if (ctx.sb.feature_incompat & ~EXT2_FEATURE_INCOMPAT_SUPP) {
        return RDNX_E_UNSUPPORTED;
    }
    if (ctx.sb.blocks_per_group == 0 || ctx.sb.inodes_per_group == 0) {
        return RDNX_E_INVALID;
    }

    ctx.block_size = 1024u << ctx.sb.log_block_size;
    if (ctx.block_size < 1024u || ctx.block_size > EXT2_MAX_BLOCK_SIZE) {
        return RDNX_E_UNSUPPORTED;
    }

    if (ctx.sb.rev_level == 0) {
        ctx.inode_size = 128u;
    } else {
        ctx.inode_size = (ctx.sb.inode_size == 0) ? 128u : ctx.sb.inode_size;
    }
    if (ctx.inode_size < 128u || ctx.inode_size > EXT2_MAX_INODE_SIZE) {
        return RDNX_E_UNSUPPORTED;
    }

    uint32_t blocks_for_groups = ctx.sb.blocks_count - ctx.sb.first_data_block;
    ctx.group_count = (blocks_for_groups + ctx.sb.blocks_per_group - 1u) / ctx.sb.blocks_per_group;
    if (ctx.group_count == 0) {
        return RDNX_E_INVALID;
    }

    uint64_t gdt_off = (uint64_t)(ctx.sb.first_data_block + 1u) * ctx.block_size;
    uint32_t gdt_size = ctx.group_count * (uint32_t)sizeof(ext2_group_desc_t);
    ctx.gdt = (ext2_group_desc_t*)kmalloc(gdt_size);
    if (!ctx.gdt) {
        return RDNX_E_NOMEM;
    }
    rc = ext2_read_bytes(&ctx, gdt_off, ctx.gdt, gdt_size);
    if (rc != RDNX_OK) {
        kfree(ctx.gdt);
        return rc;
    }

    ext2_inode_t root_ino;
    rc = ext2_read_inode(&ctx, EXT2_ROOT_INO, &root_ino);
    if (rc != RDNX_OK || !ext2_is_dir(&root_ino)) {
        kfree(ctx.gdt);
        return (rc == RDNX_OK) ? RDNX_E_INVALID : rc;
    }

    vfs_node_t* root = vfs_fs_alloc_node("/", VFS_NODE_DIR);
    if (!root) {
        kfree(ctx.gdt);
        return RDNX_E_NOMEM;
    }

    ctx.node_budget = EXT2_MAX_TREE_NODES;
    (void)ext2_build_dir(&ctx, root, EXT2_ROOT_INO, &root_ino, 0);

    ext2_mark_node(root, EXT2_ROOT_INO, &root_ino);
    *out_root = root;

    spinlock_lock(&g_ext2_rw_lock);
    if (g_ext2_live_ready && g_ext2_live.gdt) {
        kfree(g_ext2_live.gdt);
    }
    g_ext2_live = ctx;
    g_ext2_live_ready = 1;
    spinlock_unlock(&g_ext2_rw_lock);
    return RDNX_OK;
}

static const vfs_fs_driver_t ext2_driver = {
    .name = "ext2",
    .mount = ext2_mount,
};

int ext2_fs_init(void)
{
    spinlock_init(&g_ext2_rw_lock);
    (void)kmod_register_builtin("fs.ext2", "fs", "0.1", 0);
    return vfs_register_fs(&ext2_driver);
}
