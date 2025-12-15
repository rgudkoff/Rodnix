/**
 * @file riscv64/config.h
 * @brief Configuration for RISC-V64 architecture
 */

#ifndef _RODNIX_ARCH_RISCV64_CONFIG_H
#define _RODNIX_ARCH_RISCV64_CONFIG_H

#include "../../core/config.h"

/* RISC-V64 specific constants */
#define RISCV64_KERNEL_VIRT_BASE 0xFFFFFFC000000000ULL

/* RISC-V64 page sizes */
#define RISCV64_PAGE_SIZE_4KB  4096
#define RISCV64_PAGE_SIZE_2MB  2097152
#define RISCV64_PAGE_SIZE_1GB  1073741824

/* Macros for address conversion */
#define RISCV64_VIRT_TO_PHYS(addr) ((uintptr_t)(addr) - RISCV64_KERNEL_VIRT_BASE)
#define RISCV64_PHYS_TO_VIRT(addr) ((void*)((uintptr_t)(addr) + RISCV64_KERNEL_VIRT_BASE))

#endif /* _RODNIX_ARCH_RISCV64_CONFIG_H */

