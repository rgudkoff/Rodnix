#ifndef _RODNIX_KMOD_H
#define _RODNIX_KMOD_H

#include <stdint.h>
#include "../../include/kmod_abi.h"

#define KMOD_MAX 32

typedef struct kmod_info {
    char name[32];
    char kind[16];
    char version[16];
    uint32_t flags;
    uint8_t builtin;
    uint8_t loaded;
    uint8_t reserved0;
    uint8_t reserved1;
} kmod_info_t;

int kmod_init(void);
int kmod_register_builtin(const char* name, const char* kind, const char* version, uint32_t flags);
int kmod_get_info(uint32_t index, kmod_info_t* out);
uint32_t kmod_count(void);

int kmod_load(const char* path);
int kmod_unload(const char* name);

#endif /* _RODNIX_KMOD_H */
