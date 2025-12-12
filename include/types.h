/**
 * @file types.h
 * @brief Базовые типы данных
 */

#ifndef _RODNIX_TYPES_H
#define _RODNIX_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Базовые целочисленные типы */
typedef uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;

typedef int8_t   i8_t;
typedef int16_t  i16_t;
typedef int32_t  i32_t;
typedef int64_t  i64_t;

/* Указатели (64-битные) */
typedef uint64_t uintptr_t;
typedef int64_t  intptr_t;

/* Размеры */
typedef uint64_t size_t;
typedef int64_t  ssize_t;

/* NULL */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Макросы для выравнивания */
#define ALIGN_UP(addr, align)    (((addr) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))

#endif /* _RODNIX_TYPES_H */

