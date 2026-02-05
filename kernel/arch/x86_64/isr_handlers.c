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
#include "../../common/scheduler.h"
#include "../../common/syscall.h"
#include "interrupt_frame.h"
#include "types.h"
#include "config.h"
#include "pic.h"
#include "apic.h"
#include <stddef.h>


/* Minimal serial output for exception diagnostics (COM1). */
#define SERIAL_COM1_BASE 0x3F8
static inline void serial_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %%al, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t serial_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_write_char(char c)
{
    /* Wait for THR empty (bit 5) */
    for (int i = 0; i < 10000; i++) {
        if (serial_inb(SERIAL_COM1_BASE + 5) & 0x20) {
            break;
        }
    }
    serial_outb(SERIAL_COM1_BASE + 0, (uint8_t)c);
}

static void serial_write_str(const char* s)
{
    while (*s) {
        if (*s == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*s++);
    }
}

static void serial_write_hex64(uint64_t value)
{
    const char* hex = "0123456789abcdef";
    serial_write_str("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

/* Forward declaration */
extern interrupt_handler_t interrupt_handlers[256];
extern uint64_t irq_iret_rsp;
extern uint64_t irq_iret_rip;
extern uint64_t irq_iret_cs;
extern uint64_t irq_iret_rflags;
extern uint64_t isr_iret_rsp;
extern uint64_t isr_iret_rip;
extern uint64_t isr_iret_cs;
extern uint64_t isr_iret_rflags;

static void irq_send_eoi(uint32_t irq)
{
    /* EOI logic:
     * - If I/O APIC is available: use only LAPIC EOI (I/O APIC routes to LAPIC)
     * - If LAPIC is available but I/O APIC not: use both PIC and LAPIC EOI
     *   (PIC routes interrupt, but LAPIC is active, so need both)
     * - If no APIC: use only PIC EOI
     */
    extern bool ioapic_is_available(void);
    if (apic_is_available()) {
        if (ioapic_is_available()) {
            /* I/O APIC available - use only LAPIC EOI */
            apic_send_eoi();
        } else {
            /* LAPIC available but I/O APIC not - use both PIC and LAPIC EOI */
            pic_send_eoi(irq);
            apic_send_eoi();
        }
    } else {
        /* No APIC - use only PIC EOI */
        pic_send_eoi(irq);
    }
}

/* Safe VGA output function - does not use kputs/kprintf to avoid recursion */
static void safe_vga_puts(uint8_t row, uint8_t col, const char* str, uint8_t color)
{
    volatile uint16_t* vga = (volatile uint16_t*)X86_64_PHYS_TO_VIRT(0xB8000);
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
    volatile uint16_t* vga = (volatile uint16_t*)X86_64_PHYS_TO_VIRT(0xB8000);
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

static interrupt_frame_t* handle_syscall(interrupt_frame_t* regs)
{
    uint64_t ret = syscall_dispatch(regs->rax,
                                    regs->rdi,
                                    regs->rsi,
                                    regs->rdx,
                                    regs->r10,
                                    regs->r8,
                                    regs->r9);
    regs->rax = ret;
    return regs;
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
static interrupt_frame_t* interrupt_dispatch(interrupt_frame_t* regs)
{
    uint32_t vector = regs->int_no;

    if (vector == SYSCALL_VECTOR) {
        static int logged = 0;
        if (!logged) {
            extern void kputs(const char* str);
            kputs("[ISR] syscall vector 0x80\n");
            logged = 1;
        }
        return handle_syscall(regs);
    }
    
    /* Handle IRQ (32-47) - PIC IRQs are mapped to these vectors */
    if (vector >= 32 && vector < 48) {
        uint32_t irq = vector - 32;
        
        /* Validate IRQ number */
        if (irq > 15) {
            /* Invalid IRQ - send EOI and return silently */
            irq_send_eoi(irq);
            return regs;
        }
        
        /* Call registered handler if available */
        if (interrupt_handlers[vector]) {
            interrupt_context_t ctx;
            ctx.pc = regs->rip;
            ctx.sp = 0;
            ctx.flags = regs->rflags;
            ctx.error_code = regs->err_code;
            ctx.vector = vector;
            ctx.type = INTERRUPT_TYPE_IRQ;
            ctx.arch_specific = (void*)regs;
            interrupt_handlers[vector](&ctx);
        } else {
            /* Unhandled IRQ - mask it silently (no panic) */
            pic_disable_irq(irq);
        }
        
        irq_send_eoi(irq);
        if (vector == 32) {
            /* Timer tick drives preemption */
            scheduler_tick();
            regs = scheduler_switch_from_irq(regs);
        }
        return regs;
    }
    
    /* Handle exception (0-31) */
    if (vector < 32) {
        /* Call registered handler if available */
        if (interrupt_handlers[vector]) {
            interrupt_context_t ctx;
            ctx.pc = regs->rip;
            ctx.sp = 0;
            ctx.flags = regs->rflags;
            ctx.error_code = regs->err_code;
            ctx.vector = vector;
            ctx.type = INTERRUPT_TYPE_EXCEPTION;
            ctx.arch_specific = regs;
            
            interrupt_handlers[vector](&ctx);
            return regs;
        }
        
        /* Reserved exceptions (15, 22-31) - ignore silently */
        /* Exception 21 (Control Protection Exception) - can occur on some CPUs, ignore */
        if (vector == 15 || vector == 21 || (vector >= 22 && vector <= 31)) {
            /* These are reserved or can occur spuriously and should not cause panic */
            return regs;
        }
        
        /* Exception 7 (No Coprocessor) - can occur if FPU is used in interrupt handler */
        /* Silently ignore - this can happen when FPU is accessed in interrupt context */
        if (vector == 7) {
            /* This can occur if FPU is used in interrupt handler */
            /* Just return silently - FPU operations should not be done in interrupt context */
            return regs;
        }

        /* Serial exception dump for boot.log */
        serial_write_str("\n[EXC] irq_iret rsp=");
        serial_write_hex64(irq_iret_rsp);
        serial_write_str(" rip=");
        serial_write_hex64(irq_iret_rip);
        serial_write_str(" cs=");
        serial_write_hex64(irq_iret_cs);
        serial_write_str(" rflags=");
        serial_write_hex64(irq_iret_rflags);
        serial_write_str("\n");
        if (irq_iret_rsp) {
            uint64_t* p = (uint64_t*)(uintptr_t)irq_iret_rsp;
            serial_write_str("[EXC] iretq stack rip/cs/rflags=");
            serial_write_hex64(p[0]);
            serial_write_str(" ");
            serial_write_hex64(p[1]);
            serial_write_str(" ");
            serial_write_hex64(p[2]);
            serial_write_str("\n");
            serial_write_str("[EXC] iretq stack window=");
            serial_write_hex64(p[-2]);
            serial_write_str(" ");
            serial_write_hex64(p[-1]);
            serial_write_str(" ");
            serial_write_hex64(p[0]);
            serial_write_str(" ");
            serial_write_hex64(p[1]);
            serial_write_str(" ");
            serial_write_hex64(p[2]);
            serial_write_str(" ");
            serial_write_hex64(p[3]);
            serial_write_str(" ");
            serial_write_hex64(p[4]);
            serial_write_str("\n");
        }

        serial_write_str("[EXC] isr_iret rsp=");
        serial_write_hex64(isr_iret_rsp);
        serial_write_str(" rip=");
        serial_write_hex64(isr_iret_rip);
        serial_write_str(" cs=");
        serial_write_hex64(isr_iret_cs);
        serial_write_str(" rflags=");
        serial_write_hex64(isr_iret_rflags);
        serial_write_str("\n");

        serial_write_str("\n[EXC] v=");
        serial_write_hex64(vector);
        serial_write_str(" rip=");
        serial_write_hex64(regs->rip);
        serial_write_str(" err=");
        serial_write_hex64(regs->err_code);
        serial_write_str("\n");
        if (vector == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            serial_write_str("[EXC] cr2=");
            serial_write_hex64(cr2);
            serial_write_str("\n");
        }
        if (vector == 13) {
            struct {
                uint16_t limit;
                uint64_t base;
            } __attribute__((packed)) gdtr;
            __asm__ volatile ("sgdt %0" : "=m"(gdtr));
            serial_write_str("[EXC] gdtr base=");
            serial_write_hex64(gdtr.base);
            serial_write_str(" limit=");
            serial_write_hex64(gdtr.limit);
            serial_write_str("\n");
            if (gdtr.base) {
                uint64_t* gdt = (uint64_t*)(uintptr_t)gdtr.base;
                serial_write_str("[EXC] gdt[1]=");
                serial_write_hex64(gdt[1]);
                serial_write_str(" gdt[2]=");
                serial_write_hex64(gdt[2]);
                serial_write_str("\n");
            }
        }

        /* Extra minimal dump at top-left to avoid being scrolled out */
        safe_vga_puts(0, 0, "EXC v=", 0x0C);
        safe_vga_hex(0, 6, vector, 0x0C);
        safe_vga_puts(1, 0, "RIP=", 0x0C);
        safe_vga_hex(1, 4, regs->rip, 0x0C);
        safe_vga_puts(2, 0, "ERR=", 0x0C);
        safe_vga_hex(2, 4, regs->err_code, 0x0C);

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
        safe_vga_puts(20, 5, "n/a", 0x0F);
        
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
        return regs;
    }
    
    /* Unknown interrupt vector (48-255) - ignore silently */
    /* These are typically spurious interrupts or reserved vectors */
    return regs;
}

/* ISR handler (called from assembly for exceptions 0-31) */
interrupt_frame_t* isr_handler(interrupt_frame_t* regs)
{
    return interrupt_dispatch(regs);
}

/* IRQ handler (called from assembly for IRQ 32-47) */
interrupt_frame_t* irq_handler(interrupt_frame_t* regs)
{
    return interrupt_dispatch(regs);
}
