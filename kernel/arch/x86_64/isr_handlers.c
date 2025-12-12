/**
 * @file isr_handlers.c
 * @brief ISR and IRQ handler implementations
 */

#include "../../include/console.h"
#include "../../include/debug.h"
#include "../../core/interrupts.h"
#include "types.h"
#include "pic.h"
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
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

/* ISR handler (called from assembly) */
void isr_handler(struct registers* regs)
{
    /* Call registered handler if available */
    if (regs->int_no < 256 && interrupt_handlers[regs->int_no]) {
        interrupt_context_t ctx;
        /* Convert registers to context */
        ctx.pc = regs->rip;
        ctx.sp = regs->rsp;
        ctx.flags = regs->rflags;
        ctx.error_code = regs->err_code;
        ctx.vector = regs->int_no;
        ctx.type = (regs->int_no < 32) ? INTERRUPT_TYPE_EXCEPTION : INTERRUPT_TYPE_IRQ;
        ctx.arch_specific = regs;
        
        interrupt_handlers[regs->int_no](&ctx);
        return;
    }
    
    /* Default exception handler */
    if (regs->int_no < 32) {
        kputs("\n*** Exception ***\n");
        kprintf("Exception: %s (0x%x)\n", 
                exception_names[regs->int_no] ? exception_names[regs->int_no] : "Unknown",
                regs->int_no);
        kprintf("Error Code: 0x%llx\n", regs->err_code);
        kprintf("RIP: 0x%llx\n", regs->rip);
        kprintf("RSP: 0x%llx\n", regs->rsp);
        kprintf("RFLAGS: 0x%llx\n", regs->rflags);
        
        /* Page fault special handling */
        if (regs->int_no == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            kprintf("CR2: 0x%llx\n", cr2);
        }
        
        panic("Unhandled exception");
    }
}

/* IRQ handler (called from assembly) */
void irq_handler(struct registers* regs)
{
    uint32_t irq = regs->int_no - 32;
    
    /* Validate IRQ number */
    if (irq > 15) {
        /* Invalid IRQ - send EOI and return */
        pic_send_eoi(irq);
        return;
    }
    
    /* Call registered handler if available */
    if (regs->int_no < 256 && interrupt_handlers[regs->int_no]) {
        interrupt_context_t ctx;
        ctx.pc = regs->rip;
        ctx.sp = regs->rsp;
        ctx.flags = regs->rflags;
        ctx.error_code = regs->err_code;
        ctx.vector = regs->int_no;
        ctx.type = INTERRUPT_TYPE_IRQ;
        ctx.arch_specific = (void*)regs;
        
        interrupt_handlers[regs->int_no](&ctx);
    } else {
        /* Unhandled IRQ - mask it to prevent spurious interrupts */
        extern void pic_disable_irq(uint8_t irq);
        pic_disable_irq(irq);
    }
    
    /* Send EOI to PIC (always required) */
    pic_send_eoi(irq);
}

