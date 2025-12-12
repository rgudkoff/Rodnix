/**
 * @file debug.h
 * @brief Debugging utilities
 * 
 * Provides debugging macros and functions.
 */

#ifndef _RODNIX_DEBUG_H
#define _RODNIX_DEBUG_H

#include "types.h"
#include "console.h"
#include <stdbool.h>

/* ============================================================================
 * Debug levels
 * ============================================================================ */

#define DEBUG_LEVEL_NONE  0
#define DEBUG_LEVEL_ERROR 1
#define DEBUG_LEVEL_WARN  2
#define DEBUG_LEVEL_INFO  3
#define DEBUG_LEVEL_DEBUG 4
#define DEBUG_LEVEL_TRACE 5

/* ============================================================================
 * Debug configuration
 * ============================================================================ */

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL DEBUG_LEVEL_INFO
#endif

/* ============================================================================
 * Debug macros
 * ============================================================================ */

#if DEBUG_LEVEL >= DEBUG_LEVEL_ERROR
#define DEBUG_ERROR(fmt, ...) \
    do { \
        kprintf("[ERROR] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)
#else
#define DEBUG_ERROR(fmt, ...) do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_WARN
#define DEBUG_WARN(fmt, ...) \
    do { \
        kprintf("[WARN]  %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)
#else
#define DEBUG_WARN(fmt, ...) do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_INFO
#define DEBUG_INFO(fmt, ...) \
    do { \
        kprintf("[INFO]  %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)
#else
#define DEBUG_INFO(fmt, ...) do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_DEBUG
#define DEBUG_DEBUG(fmt, ...) \
    do { \
        kprintf("[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)
#else
#define DEBUG_DEBUG(fmt, ...) do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_TRACE
#define DEBUG_TRACE(fmt, ...) \
    do { \
        kprintf("[TRACE] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)
#else
#define DEBUG_TRACE(fmt, ...) do {} while(0)
#endif

/* ============================================================================
 * Assert macros
 * ============================================================================ */

#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            kprintf("[ASSERT] %s:%d: Assertion failed: %s\n", \
                    __FILE__, __LINE__, #condition); \
            __asm__ volatile ("cli; hlt"); \
        } \
    } while(0)

#define ASSERT_MSG(condition, msg) \
    do { \
        if (!(condition)) { \
            kprintf("[ASSERT] %s:%d: Assertion failed: %s - %s\n", \
                    __FILE__, __LINE__, #condition, msg); \
            __asm__ volatile ("cli; hlt"); \
        } \
    } while(0)

/* ============================================================================
 * Panic function
 * ============================================================================ */

/**
 * Panic and halt the system
 * @param msg Panic message
 */
__attribute__((noreturn)) void panic(const char* msg);

/**
 * Panic with formatted message
 * @param fmt Format string
 * @param ... Arguments
 */
__attribute__((noreturn)) void panicf(const char* fmt, ...);

#endif /* _RODNIX_DEBUG_H */

