/**
 * @file x86_64/config.h
 * @brief Configuration for x86_64 architecture
 */

#ifndef _RODNIX_ARCH_X86_64_CONFIG_H
#define _RODNIX_ARCH_X86_64_CONFIG_H

#include "../../core/config.h"

/* x86_64 specific constants */
#define X86_64_KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

/* x86_64 page sizes */
#define X86_64_PAGE_SIZE_4KB  4096
#define X86_64_PAGE_SIZE_2MB  2097152
#define X86_64_PAGE_SIZE_1GB  1073741824

/* Macros for address conversion */
#define X86_64_VIRT_TO_PHYS(addr) ((uintptr_t)(addr) - X86_64_KERNEL_VIRT_BASE)
#define X86_64_PHYS_TO_VIRT(addr) ((void*)((uintptr_t)(addr) + X86_64_KERNEL_VIRT_BASE))

#endif /* _RODNIX_ARCH_X86_64_CONFIG_H */

