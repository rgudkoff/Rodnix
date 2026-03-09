/**
 * @file block_service.c
 * @brief Fabric block storage service implementation
 */

#include "block_service.h"
#include "../fabric.h"
#include "../spin.h"
#include "service.h"
#include "../../fs/devfs.h"
#include "../../../include/common.h"
#include "../../../include/error.h"

typedef struct {
    rdnx_abi_header_t hdr;
    int (*register_device)(fabric_blockdev_t* dev);
    uint32_t (*device_count)(void);
    fabric_blockdev_t* (*device_get)(uint32_t index);
    int (*read)(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, void* out);
    int (*write)(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, const void* in);
} fabric_block_service_ops_t;

static spinlock_t g_block_lock;
static int g_block_inited = 0;
static fabric_blockdev_t* g_blockdevs[FABRIC_BLOCKDEV_MAX];
static uint32_t g_blockdev_count = 0;

static int block_service_register_device(fabric_blockdev_t* dev)
{
    return fabric_blockdev_register(dev);
}

static uint32_t block_service_device_count(void)
{
    return fabric_blockdev_count();
}

static fabric_blockdev_t* block_service_device_get(uint32_t index)
{
    return fabric_blockdev_get(index);
}

static int block_service_read(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, void* out)
{
    return fabric_blockdev_read(dev, lba, count, out);
}

static int block_service_write(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, const void* in)
{
    return fabric_blockdev_write(dev, lba, count, in);
}

static fabric_block_service_ops_t g_ops = {
    .hdr = RDNX_ABI_INIT(fabric_block_service_ops_t),
    .register_device = block_service_register_device,
    .device_count = block_service_device_count,
    .device_get = block_service_device_get,
    .read = block_service_read,
    .write = block_service_write
};

static fabric_service_t g_service = {
    .hdr = RDNX_ABI_INIT(fabric_service_t),
    .name = "storage.blkmgr",
    .ops = &g_ops,
    .context = NULL
};

int fabric_block_service_init(void)
{
    if (g_block_inited) {
        return RDNX_OK;
    }
    spinlock_init(&g_block_lock);
    for (uint32_t i = 0; i < FABRIC_BLOCKDEV_MAX; i++) {
        g_blockdevs[i] = NULL;
    }
    g_blockdev_count = 0;
    if (fabric_service_publish(&g_service) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    (void)fabric_node_set_state("/fabric/subsystems/storage/blkmgr", FABRIC_STATE_ACTIVE);
    g_block_inited = 1;
    fabric_log("[fabric-block] service ready: %s\n", g_service.name);
    return RDNX_OK;
}

int fabric_blockdev_register(fabric_blockdev_t* dev)
{
    if (!g_block_inited || !dev || !dev->name || !dev->ops || !dev->ops->read_sectors) {
        return RDNX_E_INVALID;
    }
    if (dev->hdr.abi_version != RDNX_ABI_VERSION ||
        dev->hdr.size < sizeof(fabric_blockdev_t) ||
        dev->sector_size == 0 || dev->sector_count == 0) {
        return RDNX_E_INVALID;
    }

    spinlock_lock(&g_block_lock);
    if (g_blockdev_count >= FABRIC_BLOCKDEV_MAX) {
        spinlock_unlock(&g_block_lock);
        return RDNX_E_BUSY;
    }
    for (uint32_t i = 0; i < g_blockdev_count; i++) {
        if (g_blockdevs[i] && g_blockdevs[i]->name &&
            strcmp(g_blockdevs[i]->name, dev->name) == 0) {
            spinlock_unlock(&g_block_lock);
            return RDNX_E_BUSY;
        }
    }
    g_blockdevs[g_blockdev_count++] = dev;
    spinlock_unlock(&g_block_lock);
    (void)devfs_register_blockdev(dev->name);
    fabric_log("[fabric-block] device registered: %s sectors=%llu size=%u\n",
               dev->name,
               (unsigned long long)dev->sector_count,
               dev->sector_size);
    return RDNX_OK;
}

uint32_t fabric_blockdev_count(void)
{
    spinlock_lock(&g_block_lock);
    uint32_t count = g_blockdev_count;
    spinlock_unlock(&g_block_lock);
    return count;
}

fabric_blockdev_t* fabric_blockdev_get(uint32_t index)
{
    fabric_blockdev_t* dev = NULL;
    spinlock_lock(&g_block_lock);
    if (index < g_blockdev_count) {
        dev = g_blockdevs[index];
    }
    spinlock_unlock(&g_block_lock);
    return dev;
}

fabric_blockdev_t* fabric_blockdev_find(const char* name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    spinlock_lock(&g_block_lock);
    for (uint32_t i = 0; i < g_blockdev_count; i++) {
        fabric_blockdev_t* dev = g_blockdevs[i];
        if (dev && dev->name && strcmp(dev->name, name) == 0) {
            spinlock_unlock(&g_block_lock);
            return dev;
        }
    }
    spinlock_unlock(&g_block_lock);
    return NULL;
}

int fabric_blockdev_read(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, void* out)
{
    if (!dev || !out || !dev->ops || !dev->ops->read_sectors || count == 0) {
        return RDNX_E_INVALID;
    }
    if (lba >= dev->sector_count || (dev->sector_count - lba) < count) {
        return RDNX_E_INVALID;
    }
    return dev->ops->read_sectors(dev, lba, count, out);
}

int fabric_blockdev_write(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, const void* in)
{
    if (!dev || !in || !dev->ops || count == 0) {
        return RDNX_E_INVALID;
    }
    if (dev->flags & FABRIC_BLOCKDEV_F_READONLY) {
        return RDNX_E_DENIED;
    }
    if (!dev->ops->write_sectors) {
        return RDNX_E_UNSUPPORTED;
    }
    if (lba >= dev->sector_count || (dev->sector_count - lba) < count) {
        return RDNX_E_INVALID;
    }
    return dev->ops->write_sectors(dev, lba, count, in);
}

int fabric_blockdev_get_info(uint32_t index, fabric_blockdev_info_t* out)
{
    if (!out) {
        return RDNX_E_INVALID;
    }
    memset(out, 0, sizeof(*out));
    fabric_blockdev_t* dev = fabric_blockdev_get(index);
    if (!dev || !dev->name) {
        return RDNX_E_NOTFOUND;
    }
    strncpy(out->name, dev->name, sizeof(out->name) - 1);
    out->sector_size = dev->sector_size;
    out->sector_count = dev->sector_count;
    out->flags = dev->flags;
    return RDNX_OK;
}
