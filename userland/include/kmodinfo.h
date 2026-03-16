#ifndef _RODNIX_USERLAND_KMODINFO_H
#define _RODNIX_USERLAND_KMODINFO_H

#include <stdint.h>

typedef struct rodnix_kmod_info {
    char name[32];
    char kind[16];
    char version[16];
    uint32_t flags;
    uint8_t builtin;
    uint8_t loaded;
    uint8_t reserved0;
    uint8_t reserved1;
} rodnix_kmod_info_t;

#endif /* _RODNIX_USERLAND_KMODINFO_H */
