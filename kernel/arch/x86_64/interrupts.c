/**
 * @file interrupts.c
 * @brief x86_64 interrupt subsystem implementation
 * 
 * This module provides the architecture-specific implementation of the
 * interrupt subsystem for x86_64. It integrates IDT, PIC, and interrupt
 * handler registration.
 * 
 * @note This implementation is adapted for RodNIX.
 */

#include "../../core/interrupts.h"
#include "types.h"
#include "idt.h"
#include "pic.h"
#include "apic.h"
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
volatile irql_t current_irql = IRQL_PASSIVE;

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

/* Wrapper for x86_64 interrupt handlers */
static void interrupt_wrapper(struct registers* regs)
{
    interrupt_context_t ctx;
    convert_interrupt_context(regs, &ctx);
    
    /* Call registered handler */
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
    extern void kputs(const char* str);
    
    kputs("[INT-1] Clear handlers\n");
    __asm__ volatile ("" ::: "memory");
    /* Clear all interrupt handler registrations */
    for (int i = 0; i < 256; i++) {
        interrupt_handlers[i] = NULL;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INT-2] Set IRQL\n");
    __asm__ volatile ("" ::: "memory");
    /* Set initial IRQL to PASSIVE (lowest level, interrupts allowed) */
    current_irql = IRQL_PASSIVE;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INT-3] Try APIC\n");
    __asm__ volatile ("" ::: "memory");
    /* Try APIC first, fallback to PIC */
    bool use_apic = false;
    if (apic_init() == 0 && apic_is_available()) {
        use_apic = true;
        kputs("[INT-3.1] APIC available\n");
        __asm__ volatile ("" ::: "memory");
    } else {
        kputs("[INT-3.2] APIC not available, use PIC\n");
        __asm__ volatile ("" ::: "memory");
    }
    
    kputs("[INT-4] Init PIC (early, will disable if APIC works)\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize PIC early (required for boot) */
    pic_init();
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INT-5] Mask all PIC IRQ\n");
    __asm__ volatile ("" ::: "memory");
    /* Mask all PIC IRQs initially */
    pic_disable();
    __asm__ volatile ("" ::: "memory");
    
    /* If LAPIC is available, PIC should be disabled */
    /* But if I/O APIC is not available, keep PIC for external IRQ routing */
    if (use_apic) {
        extern bool ioapic_is_available(void);
        if (ioapic_is_available()) {
            kputs("[INT-5.1] I/O APIC available, disable PIC completely\n");
            __asm__ volatile ("" ::: "memory");
            /* Disable PIC completely - all IRQ route through I/O APIC */
            pic_disable();
            __asm__ volatile ("" ::: "memory");
        } else {
            /* LAPIC available but I/O APIC not - keep PIC enabled for external IRQ */
            /* EOI will be sent via LAPIC, but IRQ routing goes through PIC */
            kputs("[INT-5.1] LAPIC available, I/O APIC not - keep PIC for external IRQ\n");
            __asm__ volatile ("" ::: "memory");
            /* Keep PIC enabled - we need it for external IRQ routing */
            /* PIC is already masked (pic_disable() was called earlier), we'll enable specific IRQs later */
        }
    }
    
    kputs("[INT-6] Init IDT\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize IDT: set up exception and IRQ handlers */
    if (idt_init() != 0) {
        return -1;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INT-OK] Done\n");
    __asm__ volatile ("" ::: "memory");
    
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

/**
 * @function interrupts_enable
 * @brief Enable interrupts
 * 
 * Interrupts are enabled by setting IRQL to PASSIVE level.
 * This function sets IRQL to PASSIVE and enables interrupts via STI.
 * 
 * @note Use IRQL-based interrupt control
 */
void interrupts_enable(void)
{
    /* Set IRQL to PASSIVE and enable interrupts */
    /* Use volatile to prevent optimization issues */
    volatile irql_t* irql_ptr = &current_irql;
    *irql_ptr = IRQL_PASSIVE;
    
    /* Enable interrupts - execute sti directly without barriers */
    __asm__ volatile ("sti");
}

/**
 * @function interrupts_disable
 * @brief Disable interrupts
 * 
 * Interrupts are disabled by setting IRQL to HIGH level.
 * This function disables interrupts via CLI and sets IRQL to HIGH.
 * 
 * @note Use IRQL-based interrupt control
 */
void interrupts_disable(void)
{
    /* Disable interrupts and set IRQL to HIGH */
    __asm__ volatile ("cli");
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    current_irql = IRQL_HIGH;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
}

irql_t get_current_irql(void)
{
    return current_irql;
}

/**
 * @function set_irql
 * @brief Set interrupt request level
 * 
 * This function sets the IRQL and enables/disables interrupts accordingly.
 * 
 * @param new_level New IRQL level
 * @return Previous IRQL level
 * 
 * @note IRQL-based interrupt control
 */
irql_t set_irql(irql_t new_level)
{
    irql_t old_level = current_irql;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    
    current_irql = new_level;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    
    /* Enable interrupts only at PASSIVE level */
    if (new_level == IRQL_PASSIVE) {
        __asm__ volatile ("sti");
        __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    } else {
        __asm__ volatile ("cli");
        __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    }
    
    return old_level;
}

void interrupt_wait(void)
{
    __asm__ volatile ("hlt");
}

int interrupt_send_ipi(uint32_t cpu_id, uint32_t vector)
{
    /* TODO: Implement IPI sending via APIC */
    (void)cpu_id;
    (void)vector;
    return -1;
}

