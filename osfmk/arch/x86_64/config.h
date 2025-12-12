/**
 * @file x86_64/config.h
 * @brief Конфигурация для архитектуры x86_64
 */

#ifndef _OSFMK_ARCH_X86_64_CONFIG_H
#define _OSFMK_ARCH_X86_64_CONFIG_H

#include "../../mach/config.h"

/* x86_64 специфичные константы */
#define X86_64_KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

/* Размеры страниц x86_64 */
#define X86_64_PAGE_SIZE_4KB  4096
#define X86_64_PAGE_SIZE_2MB  2097152
#define X86_64_PAGE_SIZE_1GB  1073741824

/* Макросы для преобразования адресов */
#define X86_64_VIRT_TO_PHYS(addr) ((uintptr_t)(addr) - X86_64_KERNEL_VIRT_BASE)
#define X86_64_PHYS_TO_VIRT(addr) ((void*)((uintptr_t)(addr) + X86_64_KERNEL_VIRT_BASE))

#endif /* _OSFMK_ARCH_X86_64_CONFIG_H */

