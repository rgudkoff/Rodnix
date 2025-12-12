/**
 * @file console.c
 * @brief Console implementation (minimal)
 */

#include "../../include/console.h"
#include <stdarg.h>

/* Simple VGA text mode implementation */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

/* VGA cursor control ports */
#define VGA_CRTC_INDEX  0x3D4
#define VGA_CRTC_DATA   0x3D5
#define VGA_CURSOR_LOW  0x0F
#define VGA_CURSOR_HIGH 0x0E

static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static uint8_t vga_row = 0;
static uint8_t vga_col = 0;
static uint8_t vga_color = 0x0F; /* White on black */

/**
 * @function update_cursor
 * @brief Update VGA text mode cursor position
 * 
 * This function updates the hardware cursor position in VGA text mode.
 * The cursor is controlled via I/O ports 0x3D4 (index) and 0x3D5 (data).
 * 
 * @param row Cursor row (0-24)
 * @param col Cursor column (0-79)
 */
static void update_cursor(uint8_t row, uint8_t col)
{
    uint16_t pos = row * VGA_WIDTH + col;
    uint8_t pos_low = (uint8_t)(pos & 0xFF);
    uint8_t pos_high = (uint8_t)((pos >> 8) & 0xFF);
    
    /* Send low byte of cursor position */
    /* Use same approach as pic.c - direct port constants in constraint */
    __asm__ volatile ("outb %%al, %1" : : "a"(VGA_CURSOR_LOW), "Nd"((uint16_t)0x3D4));
    __asm__ volatile ("outb %%al, %1" : : "a"(pos_low), "Nd"((uint16_t)0x3D5));
    
    /* Send high byte of cursor position */
    __asm__ volatile ("outb %%al, %1" : : "a"(VGA_CURSOR_HIGH), "Nd"((uint16_t)0x3D4));
    __asm__ volatile ("outb %%al, %1" : : "a"(pos_high), "Nd"((uint16_t)0x3D5));
}

void console_init(void)
{
    vga_row = 0;
    vga_col = 0;
    vga_color = 0x0F;
    /* Initialize cursor position */
    update_cursor(vga_row, vga_col);
}

void console_clear(void)
{
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
    }
    vga_row = 0;
    vga_col = 0;
    update_cursor(vga_row, vga_col);
}

/**
 * @function scroll_screen
 * @brief Scroll the screen up by one line (XNU-style)
 * 
 * This function scrolls the entire screen content up by one line,
 * clearing the bottom line. This is used when the screen is full.
 * 
 * XNU-style: Efficient scrolling using word-sized operations.
 */
static void scroll_screen(void)
{
    /* XNU-style: Copy entire screen buffer up by one line using word operations */
    /* This is more efficient than byte-by-byte copying */
    uint16_t* src = vga_buffer + VGA_WIDTH;  /* Start from line 1 */
    uint16_t* dst = vga_buffer;              /* Copy to line 0 */
    uint32_t words_to_copy = (VGA_HEIGHT - 1) * VGA_WIDTH;
    
    /* Copy all lines up by one */
    for (uint32_t i = 0; i < words_to_copy; i++) {
        dst[i] = src[i];
    }
    
    /* Clear the last line */
    uint16_t clear_char = (uint16_t)' ' | ((uint16_t)vga_color << 8);
    uint16_t* last_line = vga_buffer + (VGA_HEIGHT - 1) * VGA_WIDTH;
    for (uint8_t col = 0; col < VGA_WIDTH; col++) {
        last_line[col] = clear_char;
    }
}

void kputc(char c)
{
    /* Handle newline */
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            /* XNU-style: Scroll screen when reaching bottom */
            scroll_screen();
            vga_row = VGA_HEIGHT - 1;  /* Stay on last line after scroll */
        }
        update_cursor(vga_row, vga_col);
        return;
    }
    
    /* Handle carriage return */
    if (c == '\r') {
        vga_col = 0;
        update_cursor(vga_row, vga_col);
        return;
    }
    
    /* Handle tab (XNU-style: expand to spaces) */
    if (c == '\t') {
        do {
            uint32_t index = vga_row * VGA_WIDTH + vga_col;
            vga_buffer[index] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
            vga_col++;
            if (vga_col >= VGA_WIDTH) {
                vga_col = 0;
                vga_row++;
                if (vga_row >= VGA_HEIGHT) {
                    scroll_screen();
                    vga_row = VGA_HEIGHT - 1;
                }
            }
        } while ((vga_col & 7) != 0);  /* Tab stops every 8 columns (use bitwise AND instead of modulo) */
        return;
    }
    
    /* Write character to screen */
    uint32_t index = vga_row * VGA_WIDTH + vga_col;
    vga_buffer[index] = (uint16_t)c | ((uint16_t)vga_color << 8);
    
    /* Advance cursor */
    vga_col++;
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            /* XNU-style: Scroll screen when reaching bottom */
            scroll_screen();
            vga_row = VGA_HEIGHT - 1;  /* Stay on last line after scroll */
        }
    }
    
    /* Update hardware cursor position */
    update_cursor(vga_row, vga_col);
}

void kputs(const char* str)
{
    while (*str) {
        kputc(*str);
        str++;
    }
    /* Force immediate output - no buffering */
    __asm__ volatile ("" ::: "memory");
}

static void kprint_uint(uint64_t num, uint64_t base)
{
    char buffer[32];
    int i = 0;
    
    /* Prevent division by zero */
    if (base == 0 || base > 36) {
        base = 16; /* Default to hexadecimal */
    }
    
    if (num == 0) {
        kputc('0');
        return;
    }
    
    while (num > 0) {
        uint64_t digit = num % base;
        buffer[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        num /= base;
    }
    
    for (int j = i - 1; j >= 0; j--) {
        kputc(buffer[j]);
    }
}

void kprint_dec(uint64_t num)
{
    kprint_uint(num, 10);
}

void kprint_hex(uint64_t num)
{
    kputs("0x");
    kprint_uint(num, 16);
}

void kprint_bin(uint64_t num)
{
    kputs("0b");
    kprint_uint(num, 2);
}

void kprintf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}

void kvprintf(const char* fmt, va_list args)
{
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            
            /* Handle length modifiers (ll, l, h, hh) */
            int is_long = 0;
            if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') {
                    is_long = 2; /* ll */
                    fmt++;
                } else {
                    is_long = 1; /* l */
                }
            }
            
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int64_t val;
                    if (is_long == 2) {
                        val = (int64_t)va_arg(args, int64_t);
                    } else if (is_long == 1) {
                        val = (int64_t)va_arg(args, long);
                    } else {
                        val = (int64_t)va_arg(args, int);
                    }
                    if (val < 0) {
                        kputc('-');
                        val = -val;
                    }
                    kprint_uint((uint64_t)val, 10);
                    break;
                }
                case 'u': {
                    uint64_t val;
                    if (is_long == 2) {
                        val = va_arg(args, uint64_t);
                    } else if (is_long == 1) {
                        val = (uint64_t)va_arg(args, unsigned long);
                    } else {
                        val = (uint64_t)va_arg(args, unsigned int);
                    }
                    kprint_uint(val, 10);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t val;
                    if (is_long == 2) {
                        val = va_arg(args, uint64_t);
                    } else if (is_long == 1) {
                        val = (uint64_t)va_arg(args, unsigned long);
                    } else {
                        val = (uint64_t)va_arg(args, unsigned int);
                    }
                    kprint_hex(val);
                    break;
                }
                case 's': {
                    const char* str = va_arg(args, const char*);
                    kputs(str ? str : "(null)");
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    kputc(c);
                    break;
                }
                case '%': {
                    kputc('%');
                    break;
                }
                default:
                    kputc('%');
                    if (is_long == 2) {
                        kputc('l');
                        kputc('l');
                    } else if (is_long == 1) {
                        kputc('l');
                    }
                    kputc(*fmt);
                    break;
            }
        } else {
            kputc(*fmt);
        }
        fmt++;
    }
}

void console_set_fg_color(uint8_t color)
{
    vga_color = (vga_color & 0xF0) | (color & 0x0F);
}

void console_set_bg_color(uint8_t color)
{
    vga_color = (vga_color & 0x0F) | ((color & 0x0F) << 4);
}

void console_reset_color(void)
{
    vga_color = 0x0F;
}

