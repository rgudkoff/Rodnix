/**
 * @file isr_handlers.c
 * @brief ISR and IRQ handler implementations (XNU-style)
 * 
 * This implementation follows XNU's approach to interrupt handling:
 * - Single unified interrupt handler entry point
 * - Proper IRQ routing and EOI handling
 * - Silent handling of spurious interrupts
 * - No panic on unhandled IRQ (mask instead)
 */

#include "../../include/console.h"
#include "../../include/debug.h"
#include "../../core/interrupts.h"
#include "types.h"
#include "pic.h"
#include "apic.h"
#include <stddef.h>

/* Forward declaration */
extern interrupt_handler_t interrupt_handlers[256];

/* Register structure (matches assembly push order) */
struct registers {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rsp_orig;
    uint64_t rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
};

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
 * @brief Unified interrupt dispatcher (XNU-style)
 * 
 * This is the main interrupt handler that routes interrupts to their
 * appropriate handlers. It handles both exceptions (0-31) and IRQs (32-47).
 * 
 * @param regs Pointer to saved CPU registers
 * 
 * @note XNU-style: Single entry point, proper routing, silent spurious handling
 */
static void interrupt_dispatch(struct registers* regs)
{
    uint32_t vector = regs->int_no;
    
    /* Handle IRQ (32-47) - XNU maps PIC IRQs to these vectors */
    if (vector >= 32 && vector < 48) {
        uint32_t irq = vector - 32;
        
        /* Validate IRQ number */
        if (irq > 15) {
            /* Invalid IRQ - send EOI and return silently (XNU-style) */
            pic_send_eoi(irq);
            return;
        }
        
        /* Call registered handler if available */
        if (interrupt_handlers[vector]) {
            interrupt_context_t ctx;
            ctx.pc = regs->rip;
            ctx.sp = regs->rsp;
            ctx.flags = regs->rflags;
            ctx.error_code = regs->err_code;
            ctx.vector = vector;
            ctx.type = INTERRUPT_TYPE_IRQ;
            ctx.arch_specific = (void*)regs;
            
            interrupt_handlers[vector](&ctx);
        } else {
            /* Unhandled IRQ - mask it silently (XNU-style: no panic) */
            pic_disable_irq(irq);
        }
        
        /* Send EOI */
        /* When APIC is available, use LAPIC EOI (works for all interrupts) */
        /* PIC EOI is only needed if I/O APIC is not available */
        if (apic_is_available()) {
            /* Use LAPIC EOI (works for all interrupts when LAPIC is active) */
            apic_send_eoi();
        } else {
            /* PIC only - send PIC EOI */
            pic_send_eoi(irq);
        }
        return;
    }
    
    /* Handle exception (0-31) */
    if (vector < 32) {
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
        
        /* Reserved exceptions (15, 22-31) - ignore silently (XNU-style) */
        /* Exception 21 (Control Protection Exception) - can occur on some CPUs, ignore */
        if (vector == 15 || vector == 21 || (vector >= 22 && vector <= 31)) {
            /* These are reserved or can occur spuriously and should not cause panic */
            return;
        }
        
        /* Critical exceptions - panic (XNU-style: exceptions must be handled) */
        kputs("\n*** Exception ***\n");
        kprintf("Exception: %s (0x%x)\n", 
                exception_names[vector] ? exception_names[vector] : "Unknown",
                vector);
        kprintf("Error Code: 0x%llx\n", regs->err_code);
        kprintf("RIP: 0x%llx\n", regs->rip);
        kprintf("RSP: 0x%llx\n", regs->rsp);
        kprintf("RFLAGS: 0x%llx\n", regs->rflags);
        
        /* Page fault special handling */
        if (vector == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            kprintf("CR2: 0x%llx\n", cr2);
        }
        
        panic("Unhandled exception");
        return;
    }
    
    /* Unknown interrupt vector (48-255) - ignore silently (XNU-style) */
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
    interrupt_dispatch(regs);
}

