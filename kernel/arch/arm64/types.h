/**
 * @file arm64/types.h
 * @brief Типы данных для архитектуры ARM64
 */

#ifndef _RODNIX_ARCH_ARM64_TYPES_H
#define _RODNIX_ARCH_ARM64_TYPES_H

#include "../../core/arch_types.h"
#include "../../core/config.h"
#include <stdint.h>

/* ============================================================================
 * Регистры процессора ARM64
 * ============================================================================ */

typedef struct {
    uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
    uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
    uint64_t x16, x17, x18, x19, x20, x21, x22, x23;
    uint64_t x24, x25, x26, x27, x28, x29, x30;  /* x30 = LR */
    uint64_t sp;            /* Stack Pointer */
    uint64_t pc;            /* Program Counter */
    uint64_t pstate;        /* Processor State */
} arm64_registers_t;

/* ============================================================================
 * Контекст прерывания ARM64
 * ============================================================================ */

typedef struct {
    arm64_registers_t regs;
    uint64_t esr;           /* Exception Syndrome Register */
    uint64_t far;           /* Fault Address Register */
    uint32_t exception_type; /* Тип исключения */
} arm64_interrupt_context_t;

/* ============================================================================
 * Структура страницы ARM64 (4KB)
 * ============================================================================ */

typedef struct {
    uint64_t valid : 1;
    uint64_t table : 1;
    uint64_t ap : 2;        /* Access Permission */
    uint64_t sh : 2;        /* Shareability */
    uint64_t af : 1;        /* Access Flag */
    uint64_t nG : 1;        /* Not Global */
    uint64_t oa : 36;       /* Output Address */
    uint64_t reserved : 3;
    uint64_t dbm : 1;
    uint64_t contiguous : 1;
    uint64_t pxn : 1;       /* Privileged Execute Never */
    uint64_t uxn : 1;       /* User Execute Never */
    uint64_t ignored : 4;
} __attribute__((packed)) arm64_pte_t;

/* ============================================================================
 * Макросы для адресации страниц ARM64
 * ============================================================================ */

#define ARM64_LEVEL0_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define ARM64_LEVEL1_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define ARM64_LEVEL2_INDEX(addr) (((addr) >> 21) & 0x1FF)
#define ARM64_LEVEL3_INDEX(addr) (((addr) >> 12) & 0x1FF)
#define ARM64_PAGE_OFFSET(addr) ((addr) & 0xFFF)

/* ============================================================================
 * Константы ARM64
 * ============================================================================ */

#define ARM64_PAGE_SIZE 4096
#define ARM64_LARGE_PAGE_SIZE 2097152  /* 2MB */
#define ARM64_HUGE_PAGE_SIZE 1073741824 /* 1GB */

/* Exception Levels */
#define ARM64_EL0 0  /* User */
#define ARM64_EL1 1  /* Kernel */
#define ARM64_EL2 2  /* Hypervisor */
#define ARM64_EL3 3  /* Secure Monitor */

#endif /* _RODNIX_ARCH_ARM64_TYPES_H */

