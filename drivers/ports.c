#include "../include/ports.h"

/* Подавление предупреждений линтера о constraints в inline assembly */
/* Constraints 'a' и 'dN' валидны для GCC, но линтер их не понимает */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wasm-operand-widths"
#endif

uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0"
                      : "=a"(ret)
                      : "dN"(port));
    return ret;
}

void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1"
                      :
                      : "a"(value), "dN"(port));
}

uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile ("inw %1, %0"
                      : "=a"(ret)
                      : "dN"(port));
    return ret;
}

void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1"
                      :
                      : "a"(value), "dN"(port));
}

uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile ("inl %1, %0"
                      : "=a"(ret)
                      : "dN"(port));
    return ret;
}

void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1"
                      :
                      : "a"(value), "dN"(port));
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#pragma GCC diagnostic pop

