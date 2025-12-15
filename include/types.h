/**
 * @file types.h
 * @brief Basic data types
 */

#ifndef _RODNIX_TYPES_H
#define _RODNIX_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Basic integer types */
typedef uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;

typedef int8_t   i8_t;
typedef int16_t  i16_t;
typedef int32_t  i32_t;
typedef int64_t  i64_t;

/* Pointers and sizes are already defined in <stdint.h> and <stddef.h> */
/* In 64-bit mode they are automatically 64-bit */

/* NULL */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Alignment macros */
#define ALIGN_UP(addr, align)    (((addr) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))

#endif /* _RODNIX_TYPES_H */

