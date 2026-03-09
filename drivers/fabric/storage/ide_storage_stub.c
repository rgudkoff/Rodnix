/**
 * @file ide_storage_stub.c
 * @brief Fabric IDE storage backend (MVP: detect + publish ata0/disk0)
 */

#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/fabric/service/block_service.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include <stdbool.h>
#include <stdint.h>

#define PCI_CLASS_STORAGE 0x01u
#define PCI_SUBCLASS_IDE  0x01u
#define IDE_SLOT_MAX      2

typedef struct {
    int used;
    int present;
    fabric_device_t* dev;
    const char* ata_name;
    const char* disk_name;
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t drive_head;
    fabric_blockdev_t blockdev;
    fabric_blockdev_ops_t blockops;
} ide_slot_t;

static ide_slot_t g_slots[IDE_SLOT_MAX];

enum {
    ATA_REG_DATA = 0x00,
    ATA_REG_ERROR = 0x01,
    ATA_REG_SECCOUNT0 = 0x02,
    ATA_REG_LBA0 = 0x03,
    ATA_REG_LBA1 = 0x04,
    ATA_REG_LBA2 = 0x05,
    ATA_REG_HDDEVSEL = 0x06,
    ATA_REG_COMMAND = 0x07,
    ATA_REG_STATUS = 0x07
};

enum {
    ATA_CMD_IDENTIFY = 0xEC,
    ATA_CMD_READ_PIO = 0x20,
    ATA_CMD_WRITE_PIO = 0x30,
    ATA_CMD_CACHE_FLUSH = 0xE7
};

enum {
    ATA_SR_ERR = 0x01,
    ATA_SR_DRQ = 0x08,
    ATA_SR_DF = 0x20,
    ATA_SR_BSY = 0x80
};

static inline void ide_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t ide_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t ide_inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ide_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void ide_io_wait(const ide_slot_t* slot)
{
    if (!slot) {
        return;
    }
    /* 400ns delay by reading alternate status a few times. */
    (void)ide_inb(slot->ctrl_base);
    (void)ide_inb(slot->ctrl_base);
    (void)ide_inb(slot->ctrl_base);
    (void)ide_inb(slot->ctrl_base);
}

static int ide_wait_ready(const ide_slot_t* slot, bool want_drq)
{
    if (!slot) {
        return RDNX_E_INVALID;
    }
    for (uint32_t i = 0; i < 2000000u; i++) {
        uint8_t st = ide_inb((uint16_t)(slot->io_base + ATA_REG_STATUS));
        if (st & ATA_SR_BSY) {
            continue;
        }
        if (st & (ATA_SR_ERR | ATA_SR_DF)) {
            return RDNX_E_GENERIC;
        }
        if (!want_drq || (st & ATA_SR_DRQ)) {
            return RDNX_OK;
        }
    }
    return RDNX_E_TIMEOUT;
}

static int ide_identify(ide_slot_t* slot, uint64_t* out_sectors)
{
    if (!slot || !out_sectors) {
        return RDNX_E_INVALID;
    }
    *out_sectors = 0;

    ide_outb((uint16_t)(slot->io_base + ATA_REG_HDDEVSEL), slot->drive_head);
    ide_io_wait(slot);
    ide_outb((uint16_t)(slot->io_base + ATA_REG_SECCOUNT0), 0);
    ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA0), 0);
    ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA1), 0);
    ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA2), 0);
    ide_outb((uint16_t)(slot->io_base + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);

    uint8_t st = ide_inb((uint16_t)(slot->io_base + ATA_REG_STATUS));
    if (st == 0) {
        return RDNX_E_NOTFOUND;
    }
    if (ide_wait_ready(slot, true) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }

    uint8_t lba1 = ide_inb((uint16_t)(slot->io_base + ATA_REG_LBA1));
    uint8_t lba2 = ide_inb((uint16_t)(slot->io_base + ATA_REG_LBA2));
    if (lba1 != 0 || lba2 != 0) {
        /* ATAPI or non-ATA target; unsupported in this MVP. */
        return RDNX_E_UNSUPPORTED;
    }

    uint16_t id[256];
    for (uint32_t i = 0; i < 256; i++) {
        id[i] = ide_inw((uint16_t)(slot->io_base + ATA_REG_DATA));
    }

    uint64_t sectors28 = ((uint64_t)id[61] << 16) | (uint64_t)id[60];
    uint64_t sectors48 =
        ((uint64_t)id[103] << 48) |
        ((uint64_t)id[102] << 32) |
        ((uint64_t)id[101] << 16) |
        (uint64_t)id[100];
    uint64_t sectors = (sectors48 != 0) ? sectors48 : sectors28;
    if (sectors == 0) {
        return RDNX_E_NOTFOUND;
    }
    *out_sectors = sectors;
    return RDNX_OK;
}

static int ide_block_read(fabric_blockdev_t* bdev, uint64_t lba, uint32_t count, void* out)
{
    if (!bdev || !out || count == 0) {
        return RDNX_E_INVALID;
    }
    ide_slot_t* slot = (ide_slot_t*)bdev->context;
    if (!slot || !slot->present) {
        return RDNX_E_NOTFOUND;
    }

    uint8_t* dst = (uint8_t*)out;
    uint64_t cur_lba = lba;
    uint32_t remain = count;
    while (remain > 0) {
        uint8_t batch = (remain > 255u) ? 255u : (uint8_t)remain;

        ide_outb((uint16_t)(slot->io_base + ATA_REG_HDDEVSEL),
                 (uint8_t)(0xE0u | (slot->drive_head & 0x10u) |
                           ((cur_lba >> 24) & 0x0Fu)));
        ide_outb((uint16_t)(slot->io_base + ATA_REG_SECCOUNT0), batch);
        ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA0), (uint8_t)(cur_lba & 0xFFu));
        ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA1), (uint8_t)((cur_lba >> 8) & 0xFFu));
        ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA2), (uint8_t)((cur_lba >> 16) & 0xFFu));
        ide_outb((uint16_t)(slot->io_base + ATA_REG_COMMAND), ATA_CMD_READ_PIO);

        for (uint8_t s = 0; s < batch; s++) {
            int wrc = ide_wait_ready(slot, true);
            if (wrc != RDNX_OK) {
                return wrc;
            }
            for (uint32_t i = 0; i < 256; i++) {
                uint16_t w = ide_inw((uint16_t)(slot->io_base + ATA_REG_DATA));
                dst[0] = (uint8_t)(w & 0xFFu);
                dst[1] = (uint8_t)((w >> 8) & 0xFFu);
                dst += 2;
            }
            ide_io_wait(slot);
        }

        cur_lba += batch;
        remain -= batch;
    }
    return RDNX_OK;
}

static int ide_block_write(fabric_blockdev_t* bdev, uint64_t lba, uint32_t count, const void* in)
{
    if (!bdev || !in || count == 0) {
        return RDNX_E_INVALID;
    }
    ide_slot_t* slot = (ide_slot_t*)bdev->context;
    if (!slot || !slot->present) {
        return RDNX_E_NOTFOUND;
    }

    const uint8_t* src = (const uint8_t*)in;
    uint64_t cur_lba = lba;
    uint32_t remain = count;
    while (remain > 0) {
        uint8_t batch = (remain > 255u) ? 255u : (uint8_t)remain;

        ide_outb((uint16_t)(slot->io_base + ATA_REG_HDDEVSEL),
                 (uint8_t)(0xE0u | (slot->drive_head & 0x10u) |
                           ((cur_lba >> 24) & 0x0Fu)));
        ide_outb((uint16_t)(slot->io_base + ATA_REG_SECCOUNT0), batch);
        ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA0), (uint8_t)(cur_lba & 0xFFu));
        ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA1), (uint8_t)((cur_lba >> 8) & 0xFFu));
        ide_outb((uint16_t)(slot->io_base + ATA_REG_LBA2), (uint8_t)((cur_lba >> 16) & 0xFFu));
        ide_outb((uint16_t)(slot->io_base + ATA_REG_COMMAND), ATA_CMD_WRITE_PIO);

        for (uint8_t s = 0; s < batch; s++) {
            int wrc = ide_wait_ready(slot, true);
            if (wrc != RDNX_OK) {
                return wrc;
            }
            for (uint32_t i = 0; i < 256; i++) {
                uint16_t w = (uint16_t)src[0] | ((uint16_t)src[1] << 8);
                ide_outw((uint16_t)(slot->io_base + ATA_REG_DATA), w);
                src += 2;
            }
            ide_io_wait(slot);
        }

        /* Ensure data is committed before returning to upper layers. */
        ide_outb((uint16_t)(slot->io_base + ATA_REG_COMMAND), ATA_CMD_CACHE_FLUSH);
        if (ide_wait_ready(slot, false) != RDNX_OK) {
            return RDNX_E_TIMEOUT;
        }

        cur_lba += batch;
        remain -= batch;
    }
    return RDNX_OK;
}

static bool ide_storage_probe(fabric_device_t* dev)
{
    if (!dev) {
        return false;
    }
    return (dev->class_code == PCI_CLASS_STORAGE && dev->subclass == PCI_SUBCLASS_IDE);
}

static int ide_storage_attach(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }
    for (uint32_t i = 0; i < IDE_SLOT_MAX; i++) {
        if (!g_slots[i].used) {
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            g_slots[i].used = 1;
            g_slots[i].dev = dev;
            g_slots[i].ata_name = (i == 0) ? "ata0" : "ata1";
            g_slots[i].disk_name = (i == 0) ? "disk0" : "disk1";
            g_slots[i].io_base = (i == 0) ? 0x1F0 : 0x170;
            g_slots[i].ctrl_base = (i == 0) ? 0x3F6 : 0x376;
            g_slots[i].drive_head = 0xA0; /* master */
            g_slots[i].blockops.hdr = RDNX_ABI_INIT(fabric_blockdev_ops_t);
            g_slots[i].blockops.read_sectors = ide_block_read;
            g_slots[i].blockops.write_sectors = ide_block_write;
            g_slots[i].blockdev.hdr = RDNX_ABI_INIT(fabric_blockdev_t);
            g_slots[i].blockdev.name = g_slots[i].disk_name;
            g_slots[i].blockdev.sector_size = 512;
            g_slots[i].blockdev.sector_count = 0;
            g_slots[i].blockdev.flags = 0;
            g_slots[i].blockdev.ops = &g_slots[i].blockops;
            g_slots[i].blockdev.context = &g_slots[i];

            uint64_t sectors = 0;
            int irc = ide_identify(&g_slots[i], &sectors);
            if (irc == RDNX_OK) {
                g_slots[i].present = 1;
                g_slots[i].blockdev.sector_count = sectors;
                fabric_log("[IDE] %s: sectors=%llu (%llu MiB)\n",
                           g_slots[i].disk_name,
                           (unsigned long long)sectors,
                           (unsigned long long)((sectors * 512ULL) / (1024ULL * 1024ULL)));
            } else {
                g_slots[i].present = 0;
                fabric_log("[IDE] %s: identify failed rc=%d\n", g_slots[i].disk_name, irc);
            }

            fabric_log("[IDE] attached %s vendor=%x device=%x\n",
                       g_slots[i].ata_name, dev->vendor_id, dev->device_id);
            return RDNX_OK;
        }
    }
    return RDNX_E_BUSY;
}

static int ide_storage_publish(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }
    for (uint32_t i = 0; i < IDE_SLOT_MAX; i++) {
        if (!g_slots[i].used || g_slots[i].dev != dev) {
            continue;
        }
        if (fabric_publish_service_node(g_slots[i].ata_name, "storage", dev) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
        if (fabric_publish_service_node(g_slots[i].disk_name, "storage", dev) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
        if (g_slots[i].present) {
            (void)fabric_blockdev_register(&g_slots[i].blockdev);
        }
        return RDNX_OK;
    }
    return RDNX_E_NOTFOUND;
}

static void ide_storage_detach(fabric_device_t* dev)
{
    (void)dev;
}

static fabric_driver_t g_driver = {
    .name = "ide-storage-stub",
    .probe = ide_storage_probe,
    .attach = ide_storage_attach,
    .publish = ide_storage_publish,
    .detach = ide_storage_detach,
    .suspend = NULL,
    .resume = NULL
};

void ide_storage_stub_init(void)
{
    int rc = fabric_driver_register(&g_driver);
    if (rc == RDNX_OK) {
        kputs("[IDE] driver registered\n");
    } else {
        kputs("[IDE] driver register failed\n");
    }
}
