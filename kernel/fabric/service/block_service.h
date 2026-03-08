/**
 * @file block_service.h
 * @brief Fabric block storage service registry
 */

#ifndef _RODNIX_FABRIC_BLOCK_SERVICE_H
#define _RODNIX_FABRIC_BLOCK_SERVICE_H

#include "../../../include/abi.h"
#include <stdint.h>

#define FABRIC_BLOCKDEV_MAX 16

enum {
    FABRIC_BLOCKDEV_F_READONLY = 1u << 0,
    FABRIC_BLOCKDEV_F_SYSTEM   = 1u << 1
};

typedef struct fabric_blockdev fabric_blockdev_t;

typedef struct fabric_blockdev_ops {
    rdnx_abi_header_t hdr;
    int (*read_sectors)(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, void* out);
    int (*write_sectors)(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, const void* in);
} fabric_blockdev_ops_t;

typedef struct fabric_blockdev_info {
    char name[16];
    uint32_t sector_size;
    uint64_t sector_count;
    uint32_t flags;
} fabric_blockdev_info_t;

struct fabric_blockdev {
    rdnx_abi_header_t hdr;
    const char* name;
    uint32_t sector_size;
    uint64_t sector_count;
    uint32_t flags;
    const fabric_blockdev_ops_t* ops;
    void* context;
};

int fabric_block_service_init(void);
int fabric_blockdev_register(fabric_blockdev_t* dev);
uint32_t fabric_blockdev_count(void);
fabric_blockdev_t* fabric_blockdev_get(uint32_t index);
fabric_blockdev_t* fabric_blockdev_find(const char* name);
int fabric_blockdev_read(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, void* out);
int fabric_blockdev_write(fabric_blockdev_t* dev, uint64_t lba, uint32_t count, const void* in);
int fabric_blockdev_get_info(uint32_t index, fabric_blockdev_info_t* out);

#endif /* _RODNIX_FABRIC_BLOCK_SERVICE_H */
