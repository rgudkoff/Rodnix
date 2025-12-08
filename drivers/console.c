#include "../include/console.h"
#include "../include/ports.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEM ((volatile uint16_t*)0xB8000)

static uint8_t vga_attr = 0x0F;
static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

static uint16_t make_vga(char c)
{
    return (uint16_t)c | (uint16_t)vga_attr << 8;
}

void console_init(void)
{
    for (uint32_t y = 0; y < VGA_HEIGHT; ++y)
        for (uint32_t x = 0; x < VGA_WIDTH; ++x)
            VGA_MEM[y * VGA_WIDTH + x] = make_vga(' ');
    cursor_x = cursor_y = 0;
    /* Установить курсор в позицию 0,0 */
    uint16_t pos = 0;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void newline(void)
{
    cursor_x = 0;
    if (++cursor_y >= VGA_HEIGHT)
    {
        /* Прокрутка экрана вверх */
        for (uint32_t y = 0; y < VGA_HEIGHT - 1; ++y)
            for (uint32_t x = 0; x < VGA_WIDTH; ++x)
                VGA_MEM[y * VGA_WIDTH + x] = VGA_MEM[(y + 1) * VGA_WIDTH + x];
        /* Очистить последнюю строку */
        for (uint32_t x = 0; x < VGA_WIDTH; ++x)
            VGA_MEM[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_vga(' ');
        cursor_y = VGA_HEIGHT - 1;
    }
    /* Обновить позицию курсора */
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void kputc(char c)
{
    if (c == '\n')
    {
        newline();
        return;
    }
    VGA_MEM[cursor_y * VGA_WIDTH + cursor_x] = make_vga(c);
    if (++cursor_x >= VGA_WIDTH)
        newline();
    /* Обновить позицию курсора в VGA контроллере */
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void kputs(const char* s)
{
    while (*s) kputc(*s++);
}

void kprint_hex(uint32_t v)
{
    static const char* hex = "0123456789ABCDEF";
    kputs("0x");
    for (int i = 7; i >= 0; --i)
        kputc(hex[(v >> (i * 4)) & 0xF]);
}

void kprint_dec(uint32_t v)
{
    char buf[11];
    int pos = 0;
    if (v == 0)
    {
        kputc('0');
        return;
    }
    while (v && pos < 10)
    {
        buf[pos++] = '0' + (v % 10);
        v /= 10;
    }
    while (pos--)
        kputc(buf[pos]);
}

