#ifndef _RODNIX_FS_INITRD_H
#define _RODNIX_FS_INITRD_H

#include <stddef.h>
#include <stdint.h>

#define INITRD_MAGIC 0x52444E58u /* 'RDNX' */

typedef struct initrd_header {
    uint32_t magic;
    uint32_t entry_count;
} initrd_header_t;

typedef struct initrd_entry {
    char path[64];
    uint32_t offset;
    uint32_t size;
} initrd_entry_t;

#endif /* _RODNIX_FS_INITRD_H */
