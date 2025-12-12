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

static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static uint8_t vga_row = 0;
static uint8_t vga_col = 0;
static uint8_t vga_color = 0x0F; /* White on black */

void console_init(void)
{
    vga_row = 0;
    vga_col = 0;
    vga_color = 0x0F;
}

void console_clear(void)
{
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
    }
    vga_row = 0;
    vga_col = 0;
}

void kputc(char c)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_row = 0;
        }
        return;
    }
    
    if (c == '\r') {
        vga_col = 0;
        return;
    }
    
    uint32_t index = vga_row * VGA_WIDTH + vga_col;
    vga_buffer[index] = (uint16_t)c | ((uint16_t)vga_color << 8);
    
    vga_col++;
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_row = 0;
        }
    }
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
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int64_t val = va_arg(args, int64_t);
                    if (val < 0) {
                        kputc('-');
                        val = -val;
                    }
                    kprint_uint((uint64_t)val, 10);
                    break;
                }
                case 'u': {
                    uint64_t val = va_arg(args, uint64_t);
                    kprint_uint(val, 10);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t val = va_arg(args, uint64_t);
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

