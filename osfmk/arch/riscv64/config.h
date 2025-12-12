/**
 * @file riscv64/config.h
 * @brief Конфигурация для архитектуры RISC-V64
 */

#ifndef _OSFMK_ARCH_RISCV64_CONFIG_H
#define _OSFMK_ARCH_RISCV64_CONFIG_H

#include "../../mach/config.h"

/* RISC-V64 специфичные константы */
#define RISCV64_KERNEL_VIRT_BASE 0xFFFFFFC000000000ULL

/* Размеры страниц RISC-V64 */
#define RISCV64_PAGE_SIZE_4KB  4096
#define RISCV64_PAGE_SIZE_2MB  2097152
#define RISCV64_PAGE_SIZE_1GB  1073741824

/* Макросы для преобразования адресов */
#define RISCV64_VIRT_TO_PHYS(addr) ((uintptr_t)(addr) - RISCV64_KERNEL_VIRT_BASE)
#define RISCV64_PHYS_TO_VIRT(addr) ((void*)((uintptr_t)(addr) + RISCV64_KERNEL_VIRT_BASE))

#endif /* _OSFMK_ARCH_RISCV64_CONFIG_H */

