/**
 * @file config.h
 * @brief Конфигурация ядра
 */

#ifndef _RODNIX_CORE_CONFIG_H
#define _RODNIX_CORE_CONFIG_H

/* Определение целевой архитектуры на этапе компиляции */
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_NAME "x86_64"
    #define ARCH_FAMILY ARCH_FAMILY_X86
    #define ARCH_PTR_SIZE ARCH_PTR_SIZE_64
    #define ARCH_ISA_TYPE ARCH_TYPE_CISC
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define ARCH_NAME "arm64"
    #define ARCH_FAMILY ARCH_FAMILY_ARM
    #define ARCH_PTR_SIZE ARCH_PTR_SIZE_64
    #define ARCH_ISA_TYPE ARCH_TYPE_RISC
#elif defined(__riscv) && (__riscv_xlen == 64)
    #define ARCH_NAME "riscv64"
    #define ARCH_FAMILY ARCH_FAMILY_RISCV
    #define ARCH_PTR_SIZE ARCH_PTR_SIZE_64
    #define ARCH_ISA_TYPE ARCH_TYPE_RISC
#else
    #error "Unsupported architecture. Only 64-bit architectures are supported."
#endif

/* Размер страницы по умолчанию */
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define PAGE_MASK (~(PAGE_SIZE - 1))

/* Макросы для работы со страницами */
#define PAGE_ALIGN(addr) ALIGN_UP(addr, PAGE_SIZE)
#define PAGE_ALIGN_DOWN(addr) ALIGN_DOWN(addr, PAGE_SIZE)
#define PAGE_OFFSET(addr) ((addr) & (PAGE_SIZE - 1))
#define PAGE_FRAME(addr) ((addr) >> PAGE_SHIFT)
#define FRAME_ADDR(frame) ((frame) << PAGE_SHIFT)

#endif /* _RODNIX_CORE_CONFIG_H */

