#ifndef _RODNIX_USERLAND_DISKINFO_H
#define _RODNIX_USERLAND_DISKINFO_H

#include <stdint.h>

typedef struct rodnix_blockdev_info {
    char name[16];
    uint32_t sector_size;
    uint64_t sector_count;
    uint32_t flags;
    uint32_t reserved0;
} rodnix_blockdev_info_t;

#endif /* _RODNIX_USERLAND_DISKINFO_H */
