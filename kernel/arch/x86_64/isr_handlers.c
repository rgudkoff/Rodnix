/**
 * @file isr_handlers.c
 * @brief ISR and IRQ handler implementations
 * 
 * This implementation follows the approach to interrupt handling:
 * - Single unified interrupt handler entry point
 * - Proper IRQ routing and EOI handling
 * - Silent handling of spurious interrupts
 * - No panic on unhandled IRQ (mask instead)
 */

#include "../../../include/console.h"
#include "../../../include/debug.h"
#include "../../core/interrupts.h"
#include "types.h"
#include "pic.h"
#include "apic.h"
#include <stddef.h>

/* Forward declaration */
extern interrupt_handler_t interrupt_handlers[256];

/* Register structure (matches current assembly push order in isr_stubs.S)
 *
 * Stack layout on entry to isr_common_stub/irq_common_stub (top -> bottom):
 *   [gs][fs][es][ds]
 *   [r15][r14][r13][r12][r11][r10][r9][r8]
 *   [rbp][rdi][rsi][rdx][rcx][rbx][rax]
 *   [int_no][err_code]
 *   [rip][cs][rflags][rsp][ss]  (rsp/ss are present only on privilege change)
 *
 * This is similar in spirit to XNU's saved state structure: минимальная обработка
 * в ASM, всё остальное — в C.
 */
struct registers {
    /* Segment registers saved explicitly in stubs */
    uint64_t gs;
    uint64_t fs;
    uint64_t es;
    uint64_t ds;

    /* General-purpose registers (high to low) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    /* Interrupt metadata pushed by stubs/CPU */
    uint64_t int_no;
    uint64_t err_code;

    /* CPU-saved execution context */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* Safe VGA output function - does not use kputs/kprintf to avoid recursion */
static void safe_vga_puts(uint8_t row, uint8_t col, const char* str, uint8_t color)
{
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    uint8_t r = row;
    uint8_t c = col;
    
    while (*str && r < 25) {
        if (*str == '\n') {
            c = 0;
            r++;
        } else if (*str != '\r') {
            uint32_t idx = r * 80 + c;
            if (idx < 80 * 25) {
                vga[idx] = (uint16_t)*str | ((uint16_t)color << 8);
            }
            c++;
            if (c >= 80) {
                c = 0;
                r++;
            }
        }
        str++;
    }
}

/* Safe hex output - direct VGA write */
static void safe_vga_hex(uint8_t row, uint8_t col, uint64_t value, uint8_t color)
{
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    char hex_chars[] = "0123456789ABCDEF";
    uint8_t r = row;
    uint8_t c = col;
    
    /* Print "0x" prefix */
    if (r < 25 && c < 80) {
        uint32_t idx = r * 80 + c;
        vga[idx] = (uint16_t)'0' | ((uint16_t)color << 8);
        c++;
    }
    if (r < 25 && c < 80) {
        uint32_t idx = r * 80 + c;
        vga[idx] = (uint16_t)'x' | ((uint16_t)color << 8);
        c++;
    }
    
    /* Skip leading zeros, but always print at least one digit */
    int start = 60;
    while (start > 0 && ((value >> start) & 0xF) == 0) {
        start -= 4;
    }
    
    /* Print hex digits */
    for (int i = start; i >= 0; i -= 4) {
        if (r >= 25) break; /* Screen limit */
        if (c >= 80) {
            c = 0;
            r++;
            if (r >= 25) break;
        }
        uint8_t digit = (value >> i) & 0xF;
        uint32_t idx = r * 80 + c;
        vga[idx] = (uint16_t)hex_chars[digit] | ((uint16_t)color << 8);
        c++;
    }
}

/* Exception names */
static const char* exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt", // 15 - Reserved
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point Exception",
    "Virtualization Exception",
    "Control Protection Exception", // 21 - Reserved
    "Reserved", // 22
    "Reserved", // 23
    "Reserved", // 24
    "Reserved", // 25
    "Reserved", // 26
    "Reserved", // 27
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved" // 31
};

/**
 * @function interrupt_dispatch
 * @brief Unified interrupt dispatcher
 * 
 * This is the main interrupt handler that routes interrupts to their
 * appropriate handlers. It handles both exceptions (0-31) and IRQs (32-47).
 * 
 * @param regs Pointer to saved CPU registers
 * 
 * @note Single entry point, proper routing, silent spurious handling
 */
static void interrupt_dispatch(struct registers* regs)
{
    uint32_t vector = regs->int_no;
    
    /* DIAGNOSTIC: Mark interrupt dispatch (VGA only, bottom of screen, RED) */
    static volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
    static uint32_t dispatch_count = 0;
    if (dispatch_count < 20 && vector >= 32 && vector < 48) {
        vga_debug[80 * 21 + dispatch_count] = 0x0C00 | ('I');  /* RED */
        vga_debug[80 * 21 + dispatch_count + 1] = 0x0C00 | ('0' + ((vector - 32) % 10));  /* RED */
        dispatch_count += 2;
    }
    
    /* Handle IRQ (32-47) - PIC IRQs are mapped to these vectors */
    if (vector >= 32 && vector < 48) {
        uint32_t irq = vector - 32;
        
        /* Validate IRQ number */
        if (irq > 15) {
            /* Invalid IRQ - send EOI and return silently */
            pic_send_eoi(irq);
            return;
        }
        
        /* Call registered handler if available */
        if (interrupt_handlers[vector]) {
            if (dispatch_count < 20) {
                vga_debug[80 * 21 + dispatch_count] = 0x0C00 | ('C');  /* RED */
                dispatch_count++;
            }
            
            interrupt_context_t ctx;
            ctx.pc = regs->rip;
            ctx.sp = regs->rsp;
            ctx.flags = regs->rflags;
            ctx.error_code = regs->err_code;
            ctx.vector = vector;
            ctx.type = INTERRUPT_TYPE_IRQ;
            ctx.arch_specific = (void*)regs;
            
            interrupt_handlers[vector](&ctx);
            
            if (dispatch_count < 20) {
                vga_debug[80 * 21 + dispatch_count] = 0x0C00 | ('H');  /* RED */
                dispatch_count++;
            }
        } else {
            /* Unhandled IRQ - mask it silently (no panic) */
            if (dispatch_count < 20) {
                vga_debug[80 * 21 + dispatch_count] = 0x0C00 | ('U');  /* RED */
                dispatch_count++;
            }
            pic_disable_irq(irq);
        }
        
        /* Send EOI - CRITICAL: Must be sent before returning */
        if (dispatch_count < 20) {
            vga_debug[80 * 21 + dispatch_count] = 0x0C00 | ('E');  /* RED */
            dispatch_count++;
        }
        
        /* EOI logic:
         * - If I/O APIC is available: use only LAPIC EOI (I/O APIC routes to LAPIC)
         * - If LAPIC is available but I/O APIC not: use both PIC and LAPIC EOI
         *   (PIC routes to CPU, but LAPIC is active, so need both)
         * - If no APIC: use only PIC EOI
         */
        extern bool ioapic_is_available(void);
        if (apic_is_available()) {
            if (ioapic_is_available()) {
                /* I/O APIC available - use only LAPIC EOI */
                apic_send_eoi();
            } else {
                /* LAPIC available but I/O APIC not - use both PIC and LAPIC EOI */
                /* PIC routes interrupt, but LAPIC is active, so need both */
                pic_send_eoi(irq);
                apic_send_eoi();
            }
        } else {
            /* No APIC - use only PIC EOI */
            pic_send_eoi(irq);
        }
        
        if (dispatch_count < 20) {
            vga_debug[80 * 21 + dispatch_count] = 0x0C00 | ('X');  /* RED */
            dispatch_count++;
        }
        return;
    }
    
    /* Handle exception (0-31) */
    if (vector < 32) {
        /* DIAGNOSTIC: Mark exception on VGA (RED) - CRITICAL */
        static volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
        static uint32_t exc_count = 0;
        if (exc_count < 10) {
            vga_debug[80 * 15 + exc_count * 2] = 0x0C00 | ('E');  /* RED - Exception */
            vga_debug[80 * 15 + exc_count * 2 + 1] = 0x0C00 | ('0' + (vector % 10));  /* RED */
            exc_count += 2;
        }
        
        /* Call registered handler if available */
        if (interrupt_handlers[vector]) {
            interrupt_context_t ctx;
            ctx.pc = regs->rip;
            ctx.sp = regs->rsp;
            ctx.flags = regs->rflags;
            ctx.error_code = regs->err_code;
            ctx.vector = vector;
            ctx.type = INTERRUPT_TYPE_EXCEPTION;
            ctx.arch_specific = regs;
            
            interrupt_handlers[vector](&ctx);
            return;
        }
        
        /* Reserved exceptions (15, 22-31) - ignore silently */
        /* Exception 21 (Control Protection Exception) - can occur on some CPUs, ignore */
        if (vector == 15 || vector == 21 || (vector >= 22 && vector <= 31)) {
            /* These are reserved or can occur spuriously and should not cause panic */
            if (exc_count < 10) {
                vga_debug[80 * 15 + exc_count - 1] = 0x0C00 | ('I');  /* RED - Ignored */
            }
            return;
        }
        
        /* Exception 7 (No Coprocessor) - can occur if FPU is used in interrupt handler */
        /* Silently ignore - this can happen when FPU is accessed in interrupt context */
        if (vector == 7) {
            /* This can occur if FPU is used in interrupt handler */
            /* Just return silently - FPU operations should not be done in interrupt context */
            if (exc_count < 10) {
                vga_debug[80 * 15 + exc_count - 1] = 0x0C00 | ('7');  /* RED - Exception 7 ignored */
            }
            return;
        }
        
        /* Critical exceptions - panic (exceptions must be handled) */
        if (exc_count < 10) {
            vga_debug[80 * 15 + exc_count - 1] = 0x0C00 | ('P');  /* RED - Panic */
        }
        
        /* Use safe VGA output to avoid recursion - do NOT call kputs/kprintf */
        safe_vga_puts(16, 0, "*** Exception ***", 0x0C); /* Red */
        safe_vga_puts(17, 0, "Exception: ", 0x0F); /* White */
        if (exception_names[vector]) {
            safe_vga_puts(17, 11, exception_names[vector], 0x0F);
        } else {
            safe_vga_puts(17, 11, "Unknown", 0x0F);
        }
        safe_vga_hex(17, 40, vector, 0x0F);
        
        safe_vga_puts(18, 0, "Error Code: ", 0x0F);
        safe_vga_hex(18, 12, regs->err_code, 0x0F);
        
        safe_vga_puts(19, 0, "RIP: ", 0x0F);
        safe_vga_hex(19, 5, regs->rip, 0x0F);
        
        safe_vga_puts(20, 0, "RSP: ", 0x0F);
        safe_vga_hex(20, 5, regs->rsp, 0x0F);
        
        safe_vga_puts(21, 0, "RFLAGS: ", 0x0F);
        safe_vga_hex(21, 8, regs->rflags, 0x0F);
        
        /* Page fault special handling */
        if (vector == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            safe_vga_puts(22, 0, "CR2: ", 0x0F);
            safe_vga_hex(22, 5, cr2, 0x0F);
        }
        
        safe_vga_puts(23, 0, "*** KERNEL PANIC ***", 0x0C); /* Red */
        safe_vga_puts(24, 0, "Message: Unhandled exception", 0x0C); /* Red */
        
        /* Halt system */
        __asm__ volatile ("cli; hlt");
        for (;;) {
            __asm__ volatile ("hlt");
        }
        return;
    }
    
    /* Unknown interrupt vector (48-255) - ignore silently */
    /* These are typically spurious interrupts or reserved vectors */
}

/* ISR handler (called from assembly for exceptions 0-31) */
void isr_handler(struct registers* regs)
{
    interrupt_dispatch(regs);
}

/* IRQ handler (called from assembly for IRQ 32-47) */
void irq_handler(struct registers* regs)
{
    /* DIAGNOSTIC: Mark that irq_handler was called (VGA only, bottom of screen, RED) */
    static volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
    static uint32_t irq_handler_count = 0;
    if (irq_handler_count < 10) {
        uint32_t vector = regs->int_no;
        vga_debug[80 * 22 + irq_handler_count * 2] = 0x0C00 | ('A');  /* RED */
        vga_debug[80 * 22 + irq_handler_count * 2 + 1] = 0x0C00 | ('0' + (vector % 10));  /* RED */
        irq_handler_count++;
    }
    
    interrupt_dispatch(regs);
    
    /* DIAGNOSTIC: Mark that irq_handler is returning (RED) */
    if (irq_handler_count < 10) {
        vga_debug[80 * 22 + (irq_handler_count - 1) * 2] = 0x0C00 | ('R');  /* RED */
    }
}

