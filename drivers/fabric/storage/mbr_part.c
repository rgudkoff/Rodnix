/**
 * @file mbr_part.c
 * @brief MBR partition table scanner.
 *
 * Reads sector 0 of a block device, validates the MBR signature,
 * and registers up to 4 primary partitions as virtual block devices
 * named "<parent>p1" .. "<parent>p4".
 */

#include "mbr_part.h"
#include "../../../kernel/fabric/service/block_service.h"
#include "../../../include/common.h"
#include "../../../include/error.h"
#include "../../../include/console.h"
#include "../../../include/abi.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MBR_SIGNATURE       0xAA55u
#define MBR_SIG_OFFSET      510
#define MBR_PART_TABLE_OFF  446
#define MBR_PARTS_MAX       4

/* MBR partition entry (16 bytes, little-endian) */
typedef struct __attribute__((packed)) {
    uint8_t  status;        /* 0x80 = bootable */
    uint8_t  chs_first[3];
    uint8_t  type;          /* partition type; 0 = empty */
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t lba_count;
} mbr_entry_t;

/* Virtual block device per partition — static pool */
#define PART_POOL_MAX  (MBR_PARTS_MAX * 4)  /* up to 4 disks × 4 parts */

typedef struct {
    fabric_blockdev_t      bdev;
    fabric_blockdev_ops_t  ops;
    fabric_blockdev_t*     parent;
    uint64_t               lba_offset;
    char                   name[20];
    bool                   used;
} part_slot_t;

static part_slot_t g_part_pool[PART_POOL_MAX];

static part_slot_t* alloc_slot(void)
{
    for (int i = 0; i < PART_POOL_MAX; i++) {
        if (!g_part_pool[i].used) {
            return &g_part_pool[i];
        }
    }
    return NULL;
}

/* Read/write ops that redirect to the parent with LBA offset */
static int part_read(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, void* out)
{
    part_slot_t* slot = (part_slot_t*)dev->context;
    return fabric_blockdev_read(slot->parent, slot->lba_offset + lba, count, out);
}

static int part_write(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, const void* in)
{
    part_slot_t* slot = (part_slot_t*)dev->context;
    return fabric_blockdev_write(slot->parent, slot->lba_offset + lba, count, in);
}

int mbr_scan_and_register(fabric_blockdev_t* parent)
{
    if (!parent || parent->sector_size < 512) {
        return 0;
    }

    /* Read sector 0 */
    uint8_t mbr[512];
    if (fabric_blockdev_read(parent, 0, 1, mbr) != RDNX_OK) {
        return 0;
    }

    /* Validate signature */
    uint16_t sig = (uint16_t)((uint16_t)mbr[MBR_SIG_OFFSET] |
                               ((uint16_t)mbr[MBR_SIG_OFFSET + 1] << 8));
    if (sig != MBR_SIGNATURE) {
        return 0;  /* raw disk, no partition table — normal for QEMU raw ext2 */
    }

    int registered = 0;
    const mbr_entry_t* table = (const mbr_entry_t*)(mbr + MBR_PART_TABLE_OFF);

    for (int i = 0; i < MBR_PARTS_MAX; i++) {
        if (table[i].type == 0 || table[i].lba_count == 0) {
            continue;
        }

        part_slot_t* slot = alloc_slot();
        if (!slot) {
            klog("mbr", "partition pool full\n");
            break;
        }

        memset(slot, 0, sizeof(*slot));
        slot->used       = true;
        slot->parent     = parent;
        slot->lba_offset = (uint64_t)table[i].lba_start;

        /* Name: parent name + "p" + digit, e.g. "disk0p1" */
        size_t plen = strnlen(parent->name, 12);
        memcpy(slot->name, parent->name, plen);
        slot->name[plen]   = 'p';
        slot->name[plen+1] = (char)('1' + i);
        slot->name[plen+2] = '\0';

        slot->ops.hdr            = RDNX_ABI_INIT(fabric_blockdev_ops_t);
        slot->ops.read_sectors   = part_read;
        slot->ops.write_sectors  = part_write;

        slot->bdev.hdr          = RDNX_ABI_INIT(fabric_blockdev_t);
        slot->bdev.name         = slot->name;
        slot->bdev.sector_size  = parent->sector_size;
        slot->bdev.sector_count = (uint64_t)table[i].lba_count;
        slot->bdev.flags        = parent->flags;
        slot->bdev.ops          = &slot->ops;
        slot->bdev.context      = slot;

        int rc = fabric_blockdev_register(&slot->bdev);
        if (rc == RDNX_OK) {
            klog("mbr", "partition %s: lba=%u count=%u type=0x%02x\n",
                 slot->name,
                 table[i].lba_start,
                 table[i].lba_count,
                 table[i].type);
            registered++;
        } else {
            slot->used = false;
            klog("mbr", "partition %s: register failed rc=%d\n", slot->name, rc);
        }
    }

    return registered;
}
