/**
 * @file debug.c
 * @brief Debug implementation
 */

#include "../../include/debug.h"
#include "../../include/console.h"

__attribute__((noreturn)) void panic(const char* msg)
{
    kputs("\n\n*** KERNEL PANIC ***\n");
    if (msg) {
        kputs("Message: ");
        kputs(msg);
        kputs("\n");
    }
    kputs("System halted.\n");
    
    __asm__ volatile ("cli; hlt");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((noreturn)) void panicf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    kputs("\n\n*** KERNEL PANIC ***\n");
    kputs("Message: ");
    kvprintf(fmt, args);
    kputs("\nSystem halted.\n");
    
    va_end(args);
    
    __asm__ volatile ("cli; hlt");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

