/**
 * @file common.h
 * @brief Common utilities and macros
 * 
 * Provides common utility functions and macros.
 */

#ifndef _RODNIX_COMMON_H
#define _RODNIX_COMMON_H

#include "types.h"
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * String functions
 * ============================================================================ */

/**
 * Calculate string length
 * @param str String
 * @return Length of string
 */
size_t strlen(const char* str);

/**
 * Copy string
 * @param dest Destination buffer
 * @param src Source string
 * @return Pointer to destination
 */
char* strcpy(char* dest, const char* src);

/**
 * Copy string with length limit
 * @param dest Destination buffer
 * @param src Source string
 * @param n Maximum number of characters
 * @return Pointer to destination
 */
char* strncpy(char* dest, const char* src, size_t n);

/**
 * Compare two strings
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, negative if s1 < s2, positive if s1 > s2
 */
int strcmp(const char* s1, const char* s2);

/**
 * Compare two strings with length limit
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, negative if s1 < s2, positive if s1 > s2
 */
int strncmp(const char* s1, const char* s2, size_t n);

/**
 * Find character in string
 * @param str String to search
 * @param c Character to find
 * @return Pointer to character or NULL if not found
 */
char* strchr(const char* str, int c);

/**
 * Find substring
 * @param haystack String to search in
 * @param needle String to find
 * @return Pointer to substring or NULL if not found
 */
char* strstr(const char* haystack, const char* needle);

/* ============================================================================
 * Memory functions
 * ============================================================================ */

/**
 * Set memory to value
 * @param ptr Pointer to memory
 * @param value Value to set
 * @param size Number of bytes
 * @return Pointer to memory
 */
void* memset(void* ptr, int value, size_t size);

/**
 * Copy memory
 * @param dest Destination
 * @param src Source
 * @param size Number of bytes
 * @return Pointer to destination
 */
void* memcpy(void* dest, const void* src, size_t size);

/**
 * Move memory (handles overlapping)
 * @param dest Destination
 * @param src Source
 * @param size Number of bytes
 * @return Pointer to destination
 */
void* memmove(void* dest, const void* src, size_t size);

/**
 * Compare memory
 * @param ptr1 First memory block
 * @param ptr2 Second memory block
 * @param size Number of bytes
 * @return 0 if equal, negative if ptr1 < ptr2, positive if ptr1 > ptr2
 */
int memcmp(const void* ptr1, const void* ptr2, size_t size);

/* ============================================================================
 * Utility macros
 * ============================================================================ */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* ============================================================================
 * Alignment macros
 * ============================================================================ */

#define ALIGN_UP(addr, align)    (((addr) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))

/* ============================================================================
 * Bit manipulation macros
 * ============================================================================ */

#define BIT(n) (1UL << (n))
#define SET_BIT(val, n) ((val) |= BIT(n))
#define CLEAR_BIT(val, n) ((val) &= ~BIT(n))
#define TOGGLE_BIT(val, n) ((val) ^= BIT(n))
#define TEST_BIT(val, n) (((val) & BIT(n)) != 0)

#endif /* _RODNIX_COMMON_H */

