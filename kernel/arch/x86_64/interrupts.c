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
#include "../../core/ktime.h"
#include "types.h"
#include "idt.h"
#include "pic.h"
#include "apic.h"
#include "percpu.h"
#include "vectors.h"
#include "irqstat.h"

/* Defined in isr_handlers.c, which owns the unhandled-line policy. */
void interrupt_unmask_if_we_masked(uint32_t vector);
#include "lapic_access.h"
#include "lapic_regs.h"
#include "spin.h"
#include "interrupt_frame.h"
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Global State
 * ============================================================================ */

/* Array of registered interrupt handlers (one per vector, 0-255) */
interrupt_handler_t interrupt_handlers[256];

/* Guards interrupt_vector_reserved[] and the handler table against another
 * processor allocating the same vector at the same moment. */
static spinlock_t vector_lock;
static bool interrupt_vector_reserved[256];

/* Current Interrupt Request Level (IRQL) */
/* IRQL lives in struct percpu; these reach it by the same name the rest of
 * the file already used. */
#define current_irql (percpu_self()->irql)

/* ============================================================================
 * External References
 * ============================================================================ */

/* ISR and IRQ handlers (defined in assembly stubs) */
extern interrupt_frame_t* isr_handler(interrupt_frame_t* regs);
extern interrupt_frame_t* irq_handler(interrupt_frame_t* regs);

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
static void convert_interrupt_context(interrupt_frame_t* regs, interrupt_context_t* ctx)
{
    /* Extract basic CPU state */
    ctx->pc = regs->rip;
    ctx->sp = 0;
    ctx->flags = regs->rflags;
    ctx->error_code = regs->err_code;
    ctx->vector = regs->int_no;
    
    /* Determine interrupt type based on vector number */
    ctx->type = (regs->int_no < 32) ? INTERRUPT_TYPE_EXCEPTION : INTERRUPT_TYPE_IRQ;
    
    /* Store architecture-specific data for advanced handlers */
    static x86_64_interrupt_context_t arch_ctx_storage[256];
    x86_64_interrupt_context_t* arch_ctx = &arch_ctx_storage[regs->int_no];
    
    arch_ctx->regs.rip = regs->rip;
    arch_ctx->regs.rsp = 0;
    arch_ctx->regs.rflags = regs->rflags;
    arch_ctx->error_code = regs->err_code;
    arch_ctx->vector = regs->int_no;
    
    ctx->arch_specific = arch_ctx;
}

/* Wrapper for x86_64 interrupt handlers */
static void __attribute__((unused)) interrupt_wrapper(interrupt_frame_t* regs)
{
    interrupt_context_t ctx;
    convert_interrupt_context(regs, &ctx);
    
    /* Call registered handler */
    if (interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](&ctx);
    }
}

/* Forward declarations */
interrupt_frame_t* isr_handler(interrupt_frame_t* regs);
interrupt_frame_t* irq_handler(interrupt_frame_t* regs);

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
        interrupt_vector_reserved[i] = false;
    }
    for (int i = 0; i < 32; i++) {
        interrupt_vector_reserved[i] = true;   /* CPU exceptions */
    }
    for (int i = 32; i < 48; i++) {
        interrupt_vector_reserved[i] = true;   /* legacy IRQ window */
    }
    interrupt_vector_reserved[128] = true;     /* syscall gate */
    interrupt_vector_reserved[VECTOR_RESCHED] = true;        /* scheduler entry */
    interrupt_vector_reserved[VECTOR_TLB_SHOOTDOWN] = true;  /* TLB shootdown */
    for (uint32_t i = VECTOR_LAPIC_TIMER; i <= 0xEFu; i++) {
        interrupt_vector_reserved[i] = true;            /* timer class */
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INT-2] Set IRQL\n");
    __asm__ volatile ("" ::: "memory");
    /* Set initial IRQL to PASSIVE (lowest level, interrupts allowed) */
    current_irql = IRQL_PASSIVE;
    __asm__ volatile ("" ::: "memory");
    
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
    
    kputs("[INT-6] Init IDT\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize IDT: set up exception and IRQ handlers */
    if (idt_init() != 0) {
        return -1;
    }
    __asm__ volatile ("" ::: "memory");

    kputs("[INT-OK] Done\n");
    __asm__ volatile ("" ::: "memory");
    
    return 0;
}

int interrupt_register(uint32_t vector, interrupt_handler_t handler)
{
    if (vector >= 256 || !handler) {
        return -1;
    }

    /* Refuse rather than overwrite. Silently replacing a handler means the
     * driver that initialised last owns the vector and nobody is told; that
     * is what happened on vector 32, claimed by both the PIT and the LAPIC
     * timer (docs/ru/irq_audit.md, F3). A deliberate handover calls
     * interrupt_unregister() first, which says so at the call site. */
    uint64_t flags = spinlock_lock_irqsave(&vector_lock);
    if (interrupt_handlers[vector] != NULL) {
        spinlock_unlock_irqrestore(&vector_lock, flags);
        return -1;
    }
    interrupt_handlers[vector] = handler;
    spinlock_unlock_irqrestore(&vector_lock, flags);

    /* A driver arriving after the line was shut off gets it back. Masking an
     * unhandled line is a defence against a runaway, not a permanent verdict
     * on the device (docs/ru/irq_audit.md, F8). */
    irqstat_clear_streak(vector);
    interrupt_unmask_if_we_masked(vector);

    return 0;
}

int interrupt_unregister(uint32_t vector)
{
    if (vector >= 256) {
        return -1;
    }

    uint64_t flags = spinlock_lock_irqsave(&vector_lock);
    interrupt_handlers[vector] = NULL;
    spinlock_unlock_irqrestore(&vector_lock, flags);
    return 0;
}

int interrupt_vector_alloc(uint32_t min_vector, uint32_t max_vector)
{
    if (min_vector < 32u) {
        min_vector = 32u;
    }
    if (max_vector >= 256u) {
        max_vector = 255u;
    }
    if (min_vector > max_vector) {
        return -1;
    }

    uint64_t flags = spinlock_lock_irqsave(&vector_lock);
    for (uint32_t vector = min_vector; vector <= max_vector; vector++) {
        if (!interrupt_vector_reserved[vector] && interrupt_handlers[vector] == NULL) {
            interrupt_vector_reserved[vector] = true;
            spinlock_unlock_irqrestore(&vector_lock, flags);
            return (int)vector;
        }
    }
    spinlock_unlock_irqrestore(&vector_lock, flags);
    return -1;
}

void interrupt_vector_free(uint32_t vector)
{
    if (vector >= 256u) {
        return;
    }
    if (vector < 32u || (vector >= 32u && vector < 48u) || vector == 128u) {
        return;
    }

    uint64_t flags = spinlock_lock_irqsave(&vector_lock);
    interrupt_vector_reserved[vector] = false;
    spinlock_unlock_irqrestore(&vector_lock, flags);
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
    current_irql = IRQL_PASSIVE;
    
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
    return (irql_t)current_irql;
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
/*
 * IRQL as a Task Priority Register value.
 *
 * Every level below HIGH is a real priority: the local APIC withholds any
 * interrupt whose vector class is at or below the TPR, so raising the level
 * masks exactly the sources vectors.h places at or beneath it and leaves the
 * rest running. This used to be seven declared levels of which two did
 * anything, with the five in between masking nothing at all
 * (docs/ru/irq_audit.md, F5).
 *
 * IRQL_HIGH keeps its old meaning of interrupts off outright, because the TPR
 * cannot express it: even a full TPR still admits NMI, SMI and #MC, and code
 * at HIGH is holding a lock an interrupt handler might take.
 *
 * Before the LAPIC is up there is no TPR to write, so every level falls back
 * to the previous behaviour of masking everything but PASSIVE.
 */
static uint32_t irql_to_tpr(irql_t level)
{
    switch (level) {
    case IRQL_PASSIVE:  return TPR_PASSIVE;
    case IRQL_APC:      return TPR_APC;
    case IRQL_DISPATCH: return TPR_DISPATCH;
    case IRQL_DEVICE:   return TPR_DEVICE;
    case IRQL_CLOCK:    return TPR_CLOCK;
    case IRQL_IPI:      return TPR_IPI;
    default:            return TPR_IPI;
    }
}

irql_t set_irql(irql_t new_level)
{
    irql_t old_level = (irql_t)current_irql;
    __asm__ volatile ("" ::: "memory");

    current_irql = new_level;
    __asm__ volatile ("" ::: "memory");

    if (new_level == IRQL_HIGH) {
        __asm__ volatile ("cli");
        __asm__ volatile ("" ::: "memory");
        return old_level;
    }

    if (!lapic_access_ready()) {
        /* No TPR yet: the pre-APIC fallback, where anything above PASSIVE can
         * only mean "off". */
        if (new_level == IRQL_PASSIVE) {
            __asm__ volatile ("sti");
        } else {
            __asm__ volatile ("cli");
        }
        __asm__ volatile ("" ::: "memory");
        return old_level;
    }

    lapic_access_write(APIC_TPR, irql_to_tpr(new_level));
    __asm__ volatile ("" ::: "memory");
    /* Below HIGH the processor takes interrupts; which ones is now the TPR's
     * decision rather than a blanket cli. */
    __asm__ volatile ("sti");
    __asm__ volatile ("" ::: "memory");

    return old_level;
}

void interrupt_trigger_resched(void)
{
    __asm__ volatile ("int %0"
                      :
                      : "i"(VECTOR_RESCHED)
                      : "rax", "rcx", "rdx", "rsi", "rdi",
                        "r8", "r9", "r10", "r11", "cc", "memory");
}

void interrupt_wait(void)
{
    __asm__ volatile ("hlt");
}

int interrupt_send_ipi(uint32_t cpu_id, uint32_t vector)
{
    /* cpu_id is the kernel's own CPU index -- what cpu_get_id() returns --
     * not an APIC ID. The mapping between them lives in the per-CPU slot,
     * which is filled when a processor is brought up. */
    if (vector > 255u) {
        return -1;
    }

    if (cpu_id == percpu_index()) {
        /* Self-addressed: the shorthand avoids needing our own APIC ID and
         * is what makes a send-to-self work identically in both LAPIC modes. */
        return apic_send_ipi_self((uint8_t)vector);
    }

    const struct percpu* target = percpu_peer(cpu_id);
    if (!target) {
        return -1;
    }
    return apic_send_ipi(target->apic_id, (uint8_t)vector);
}

/*
 * Prove the levels actually mask.
 *
 * Raise to IRQL_CLOCK, which vectors.h places at or above the LAPIC timer,
 * and spin: no tick may land. Drop to PASSIVE and spin the same amount: ticks
 * must resume. Without this the TPR write is unfalsifiable -- a level that
 * silently masks nothing looks exactly like one that works.
 *
 * Runs once, after the timer is live. Bounded by construction: the spins are
 * fixed-length, so a broken TPR fails the check instead of stalling.
 */
int irql_selftest(void)
{
    /* This CPU's own tick count, not the machine-wide one. The global
     * counter is incremented by every processor, so on a multiprocessor
     * machine it keeps climbing while this processor's timer is masked and
     * the check reports a failure that is entirely its own confusion. */
    const uint32_t self = percpu_index();
#define TICKS_HERE() ((uint32_t)irqstat_get(self, VECTOR_LAPIC_TIMER))

    if (!lapic_access_ready() || !ktime_ready()) {
        return -1;
    }

    /* Both windows are sized in wall-clock time, not iterations: an
     * iteration count that spans ten timer periods on hardware spans
     * seconds under TCG emulation. 10ms covers several periods at any
     * plausible timer rate, which is all either verdict needs. */
    const uint64_t window_ns = 10000000ULL;

    irql_t entry = set_irql(IRQL_CLOCK);
    uint32_t masked_start = TICKS_HERE();
    uint64_t t0 = ktime_ns();
    while (ktime_ns() - t0 < window_ns) {
        __asm__ volatile ("pause");
    }
    uint32_t masked_end = TICKS_HERE();

    (void)set_irql(IRQL_PASSIVE);
    uint32_t open_start = TICKS_HERE();
    uint32_t open_end = open_start;
    t0 = ktime_ns();
    while (open_end == open_start && ktime_ns() - t0 < window_ns) {
        __asm__ volatile ("pause");
        open_end = TICKS_HERE();
    }

    (void)set_irql(entry);

    bool masked_ok = (masked_end == masked_start);
    bool open_ok = (open_end > open_start);
    return (masked_ok && open_ok) ? 0 : -1;
#undef TICKS_HERE
}
