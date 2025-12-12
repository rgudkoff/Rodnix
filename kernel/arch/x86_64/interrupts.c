/**
 * @file interrupts.c
 * @brief x86_64 interrupt subsystem implementation
 * 
 * This module provides the architecture-specific implementation of the
 * interrupt subsystem for x86_64. It integrates IDT, PIC, and interrupt
 * handler registration.
 * 
 * @note This implementation follows XNU-style architecture but is adapted for RodNIX.
 */

#include "../../core/interrupts.h"
#include "types.h"
#include "idt.h"
#include "pic.h"
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Register Structure (matches assembly push order)
 * ============================================================================ */

/**
 * @struct registers
 * @brief CPU register state saved during interrupt
 * 
 * This structure matches the order in which registers are pushed onto
 * the stack by the interrupt handler stubs in isr_stubs.S.
 * 
 * @note The order is: r15, r14, ..., r8, rdi, rsi, rbp, rbx, rdx, rcx, rax,
 *       then error_code, int_no, then rip, cs, rflags, rsp, ss.
 */
struct registers {
    uint64_t r15, r14, r13, r12;      /* General purpose registers R15-R12 */
    uint64_t r11, r10, r9, r8;         /* General purpose registers R11-R8 */
    uint64_t rdi, rsi, rbp, rsp_orig;  /* General purpose registers RDI, RSI, RBP, RSP */
    uint64_t rbx, rdx, rcx, rax;       /* General purpose registers RBX, RDX, RCX, RAX */
    uint64_t int_no, err_code;         /* Interrupt number and error code */
    uint64_t rip, cs, rflags;          /* Instruction pointer, code segment, flags */
    uint64_t rsp, ss;                  /* Stack pointer, stack segment */
};

/* ============================================================================
 * Global State
 * ============================================================================ */

/* Array of registered interrupt handlers (one per vector, 0-255) */
interrupt_handler_t interrupt_handlers[256];

/* Current Interrupt Request Level (IRQL) */
static irql_t current_irql = IRQL_PASSIVE;

/* ============================================================================
 * External References
 * ============================================================================ */

/* ISR and IRQ handlers (defined in assembly stubs) */
extern void isr_handler(struct registers* regs);
extern void irq_handler(struct registers* regs);

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @function convert_interrupt_context
 * @brief Convert x86_64 register structure to architecture-independent context
 * 
 * @param regs Pointer to x86_64 register structure (from assembly)
 * @param ctx Pointer to architecture-independent interrupt context (output)
 * 
 * @note This function extracts relevant information from the x86_64-specific
 *       register structure and populates the generic interrupt context.
 */
static void convert_interrupt_context(struct registers* regs, interrupt_context_t* ctx)
{
    /* Extract basic CPU state */
    ctx->pc = regs->rip;
    ctx->sp = regs->rsp;
    ctx->flags = regs->rflags;
    ctx->error_code = regs->err_code;
    ctx->vector = regs->int_no;
    
    /* Determine interrupt type based on vector number */
    ctx->type = (regs->int_no < 32) ? INTERRUPT_TYPE_EXCEPTION : INTERRUPT_TYPE_IRQ;
    
    /* Store architecture-specific data for advanced handlers */
    static x86_64_interrupt_context_t arch_ctx_storage[256];
    x86_64_interrupt_context_t* arch_ctx = &arch_ctx_storage[regs->int_no];
    
    arch_ctx->regs.rip = regs->rip;
    arch_ctx->regs.rsp = regs->rsp;
    arch_ctx->regs.rflags = regs->rflags;
    arch_ctx->error_code = regs->err_code;
    arch_ctx->vector = regs->int_no;
    
    ctx->arch_specific = arch_ctx;
}

/* Обертка для обработчиков прерываний x86_64 */
static void interrupt_wrapper(struct registers* regs)
{
    interrupt_context_t ctx;
    convert_interrupt_context(regs, &ctx);
    
    /* Вызываем зарегистрированный обработчик */
    if (interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](&ctx);
    }
}

/* Forward declarations */
void isr_handler(struct registers* regs);
void irq_handler(struct registers* regs);

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function interrupts_init
 * @brief Initialize the interrupt subsystem
 * 
 * This function performs the following initialization steps:
 * 1. Clears all interrupt handler registrations
 * 2. Sets initial IRQL to PASSIVE (interrupts enabled)
 * 3. Initializes the PIC (Programmable Interrupt Controller)
 * 4. Initializes the IDT (Interrupt Descriptor Table)
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note This must be called before any interrupts can be handled.
 * @note After this function returns, exceptions and IRQs are routed to
 *       their respective handlers, but interrupts remain disabled until
 *       interrupts_enable() is called.
 */
int interrupts_init(void)
{
    /* Clear all interrupt handler registrations */
    for (int i = 0; i < 256; i++) {
        interrupt_handlers[i] = NULL;
    }
    
    /* Set initial IRQL to PASSIVE (lowest level, interrupts allowed) */
    current_irql = IRQL_PASSIVE;
    
    /* Initialize PIC: remap IRQs to vectors 32-47 */
    pic_init();
    
    /* Mask all IRQs initially (XNU-style: enable only what we need) */
    pic_disable();
    
    /* Initialize IDT: set up exception and IRQ handlers */
    if (idt_init() != 0) {
        return -1;
    }
    
    /* TODO: Initialize APIC (Advanced Programmable Interrupt Controller)
     *       if available. APIC is preferred on modern systems but PIC
     *       initialization is still required for compatibility.
     */
    
    return 0;
}

int interrupt_register(uint32_t vector, interrupt_handler_t handler)
{
    if (vector >= 256 || !handler) {
        return -1;
    }
    
    interrupt_handlers[vector] = handler;
    
    return 0;
}

int interrupt_unregister(uint32_t vector)
{
    if (vector >= 256) {
        return -1;
    }
    
    interrupt_handlers[vector] = NULL;
    return 0;
}

void interrupts_enable(void)
{
    __asm__ volatile ("sti");
}

void interrupts_disable(void)
{
    __asm__ volatile ("cli");
}

irql_t get_current_irql(void)
{
    return current_irql;
}

irql_t set_irql(irql_t new_level)
{
    irql_t old_level = current_irql;
    current_irql = new_level;
    
    /* Если переходим на более высокий уровень, отключаем прерывания */
    if (new_level > IRQL_PASSIVE) {
        interrupts_disable();
    } else {
        interrupts_enable();
    }
    
    return old_level;
}

void interrupt_wait(void)
{
    __asm__ volatile ("hlt");
}

int interrupt_send_ipi(uint32_t cpu_id, uint32_t vector)
{
    /* TODO: Реализовать отправку IPI через APIC */
    (void)cpu_id;
    (void)vector;
    return -1;
}

