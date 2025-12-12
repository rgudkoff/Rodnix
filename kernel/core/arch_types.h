/**
 * @file arch_types.h
 * @brief Архитектурно-независимые типы данных
 * 
 * Определяет базовые типы и константы, используемые во всех архитектурах.
 */

#ifndef _RODNIX_CORE_ARCH_TYPES_H
#define _RODNIX_CORE_ARCH_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Архитектурные семейства
 * ============================================================================ */

typedef enum {
    ARCH_FAMILY_UNKNOWN = 0,
    ARCH_FAMILY_X86,      /* x86/x86_64 (CISC) */
    ARCH_FAMILY_ARM,      /* ARM/ARM64 (RISC) */
    ARCH_FAMILY_RISCV,    /* RISC-V (RISC) */
    ARCH_FAMILY_MIPS,     /* MIPS (RISC) */
    ARCH_FAMILY_PPC,      /* PowerPC (RISC) */
} arch_family_t;

/* ============================================================================
 * Тип архитектуры (ISA)
 * ============================================================================ */

typedef enum {
    ARCH_TYPE_UNKNOWN = 0,
    ARCH_TYPE_CISC,   /* Complex Instruction Set Computer */
    ARCH_TYPE_RISC,   /* Reduced Instruction Set Computer */
} arch_type_t;

/* ============================================================================
 * Размер указателя
 * ============================================================================ */

typedef enum {
    ARCH_PTR_SIZE_32 = 32,
    ARCH_PTR_SIZE_64 = 64,
} arch_ptr_size_t;

/* ============================================================================
 * Информация об архитектуре
 * ============================================================================ */

typedef struct {
    arch_family_t family;
    arch_ptr_size_t ptr_size;
    arch_type_t isa_type;
    const char* name;
    const char* vendor;
    uint32_t features;  /* Архитектурные особенности */
} arch_info_t;

/* ============================================================================
 * Константы страниц памяти
 * ============================================================================ */

#define ARCH_PAGE_SIZE_MIN    4096        /* 4KB - минимальный размер страницы */
#define ARCH_PAGE_SIZE_LARGE  2097152     /* 2MB - большая страница */
#define ARCH_PAGE_SIZE_HUGE   1073741824  /* 1GB - огромная страница */

/* ============================================================================
 * Макросы для определения архитектуры
 * ============================================================================ */

#ifndef ARCH_NAME
#error "ARCH_NAME must be defined (e.g., x86_64, arm64, riscv64)"
#endif

#ifndef ARCH_FAMILY
#error "ARCH_FAMILY must be defined"
#endif

#ifndef ARCH_PTR_SIZE
#error "ARCH_PTR_SIZE must be defined (32 or 64)"
#endif

#ifndef ARCH_ISA_TYPE
#error "ARCH_ISA_TYPE must be defined (ARCH_TYPE_CISC or ARCH_TYPE_RISC)"
#endif

#endif /* _RODNIX_CORE_ARCH_TYPES_H */

