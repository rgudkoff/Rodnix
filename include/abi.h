/**
 * @file abi.h
 * @brief Userland ABI definitions
 */

#ifndef _RODNIX_ABI_H
#define _RODNIX_ABI_H

#include <stdint.h>

/* ABI version for userland/kernel boundary */
#define RDNX_ABI_VERSION 1

typedef struct {
    uint32_t abi_version; /* RDNX_ABI_VERSION */
    uint32_t size;        /* sizeof(struct) */
} rdnx_abi_header_t;

#define RDNX_ABI_INIT(_type) \
    (rdnx_abi_header_t){ .abi_version = RDNX_ABI_VERSION, .size = sizeof(_type) }

#endif /* _RODNIX_ABI_H */
