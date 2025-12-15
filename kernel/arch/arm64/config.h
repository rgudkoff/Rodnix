/**
 * @file arm64/config.h
 * @brief Configuration for ARM64 architecture
 */

#ifndef _RODNIX_ARCH_ARM64_CONFIG_H
#define _RODNIX_ARCH_ARM64_CONFIG_H

#include "../../core/config.h"

/* ARM64 specific constants */
#define ARM64_KERNEL_VIRT_BASE 0xFFFF000000000000ULL

/* ARM64 page sizes */
#define ARM64_PAGE_SIZE_4KB  4096
#define ARM64_PAGE_SIZE_2MB  2097152
#define ARM64_PAGE_SIZE_1GB  1073741824

/* Macros for address conversion */
#define ARM64_VIRT_TO_PHYS(addr) ((uintptr_t)(addr) - ARM64_KERNEL_VIRT_BASE)
#define ARM64_PHYS_TO_VIRT(addr) ((void*)((uintptr_t)(addr) + ARM64_KERNEL_VIRT_BASE))

#endif /* _RODNIX_ARCH_ARM64_CONFIG_H */

