#ifndef _RODNIX_KMOD_ABI_H
#define _RODNIX_KMOD_ABI_H

#include <stdint.h>

#define KMOD_IMAGE_MAGIC "RDKMOD1"
#define KMOD_IMAGE_MAGIC_LEN 7u

typedef struct kmod_image_header {
    char magic[8];
    char name[32];
    char kind[16];
    char version[16];
    uint32_t flags;
    uint32_t image_size;
    uint32_t reserved0;
    uint32_t reserved1;
} kmod_image_header_t;

#endif /* _RODNIX_KMOD_ABI_H */
