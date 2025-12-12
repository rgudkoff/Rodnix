/**
 * @file console.h
 * @brief Console output interface
 * 
 * Provides functions for console output and input.
 */

#ifndef _RODNIX_CONSOLE_H
#define _RODNIX_CONSOLE_H

#include "types.h"
#include <stdarg.h>

/* ============================================================================
 * Console colors
 * ============================================================================ */

#define CONSOLE_COLOR_BLACK   0
#define CONSOLE_COLOR_BLUE    1
#define CONSOLE_COLOR_GREEN   2
#define CONSOLE_COLOR_CYAN    3
#define CONSOLE_COLOR_RED     4
#define CONSOLE_COLOR_MAGENTA 5
#define CONSOLE_COLOR_BROWN   6
#define CONSOLE_COLOR_GRAY    7
#define CONSOLE_COLOR_DARK_GRAY    8
#define CONSOLE_COLOR_LIGHT_BLUE   9
#define CONSOLE_COLOR_LIGHT_GREEN  10
#define CONSOLE_COLOR_LIGHT_CYAN   11
#define CONSOLE_COLOR_LIGHT_RED    12
#define CONSOLE_COLOR_LIGHT_MAGENTA 13
#define CONSOLE_COLOR_YELLOW  14
#define CONSOLE_COLOR_WHITE   15

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize console
 */
void console_init(void);

/**
 * Clear console screen
 */
void console_clear(void);

/* ============================================================================
 * Output functions
 * ============================================================================ */

/**
 * Print a character
 * @param c Character to print
 */
void kputc(char c);

/**
 * Print a string
 * @param str String to print
 */
void kputs(const char* str);

/**
 * Print formatted string
 * @param fmt Format string
 * @param ... Arguments
 */
void kprintf(const char* fmt, ...);

/**
 * Print formatted string with va_list
 * @param fmt Format string
 * @param args Arguments
 */
void kvprintf(const char* fmt, va_list args);

/* ============================================================================
 * Utility functions
 * ============================================================================ */

/**
 * Print decimal number
 * @param num Number to print
 */
void kprint_dec(uint64_t num);

/**
 * Print hexadecimal number
 * @param num Number to print
 */
void kprint_hex(uint64_t num);

/**
 * Print binary number
 * @param num Number to print
 */
void kprint_bin(uint64_t num);

/* ============================================================================
 * Color functions
 * ============================================================================ */

/**
 * Set foreground color
 * @param color Color to set
 */
void console_set_fg_color(uint8_t color);

/**
 * Set background color
 * @param color Color to set
 */
void console_set_bg_color(uint8_t color);

/**
 * Reset colors to default
 */
void console_reset_color(void);

#endif /* _RODNIX_CONSOLE_H */

