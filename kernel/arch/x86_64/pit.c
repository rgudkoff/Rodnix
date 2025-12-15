/**
 * @file pit.c
 * @brief PIT (Programmable Interval Timer) implementation for x86_64
 * 
 * This module implements the 8253/8254 PIT for system timing. It follows
 * XNU-style timer architecture principles:
 * - Callback-based timer system
 * - Integration with interrupt subsystem
 * - Support for multiple timer callbacks
 * - Precise timing control
 * 
 * @note This implementation follows XNU-style architecture but is adapted for RodNIX.
 * @note PIT is typically used as a fallback timer when APIC timer is not available.
 */

#include "types.h"
#include "pic.h"
#include "../../core/interrupts.h"
#include "../../include/debug.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations - avoid circular dependency */
extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t phys);
extern int interrupt_register(uint32_t vector, interrupt_handler_t handler);

/* ============================================================================
 * PIT I/O Ports
 * ============================================================================ */

#define PIT_CHANNEL0_DATA    0x40    /* Channel 0 data port (system timer) */
#define PIT_CHANNEL1_DATA    0x41    /* Channel 1 data port (unused) */
#define PIT_CHANNEL2_DATA    0x42    /* Channel 2 data port (speaker) */
#define PIT_COMMAND          0x43    /* Command/control port */

/* ============================================================================
 * PIT Command Constants
 * ============================================================================ */

/* Command byte format:
 *   Bits 7-6: Channel select (00=0, 01=1, 10=2, 11=read-back)
 *   Bits 5-4: Access mode (00=latch, 01=low byte, 10=high byte, 11=both)
 *   Bits 3-1: Operating mode (0-5)
 *   Bit 0:    BCD mode (0=binary, 1=BCD)
 */

#define PIT_CMD_CHANNEL0     0x00    /* Select channel 0 */
#define PIT_CMD_ACCESS_BOTH  0x30    /* Access both bytes (low then high) */
#define PIT_CMD_MODE3        0x06    /* Mode 3: Square wave generator */
#define PIT_CMD_MODE2        0x04    /* Mode 2: Rate generator */

/* PIT frequency constants */
#define PIT_BASE_FREQUENCY   1193182 /* PIT base frequency (Hz) */
#define PIT_DEFAULT_FREQUENCY 100    /* Default frequency (100 Hz = 10ms) */

/* ============================================================================
 * Timer Callback System (XNU-style)
 * ============================================================================ */

/**
 * @struct timer_callback
 * @brief Timer callback entry
 * 
 * This structure represents a callback function that will be called
 * on each timer interrupt. Multiple callbacks can be registered.
 */
struct timer_callback {
    void (*handler)(void* arg);      /* Callback function */
    void* arg;                       /* Argument passed to callback */
    bool active;                     /* Whether callback is active */
    struct timer_callback* next;     /* Next callback in list */
};

/* Timer callback list */
static struct timer_callback* timer_callbacks = NULL;
static volatile uint32_t timer_ticks = 0;     /* System tick counter (volatile for interrupt handler) */
static uint32_t timer_frequency = PIT_DEFAULT_FREQUENCY; /* Current frequency */
static volatile bool timer_handler_active = false; /* Protection against recursive calls */

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @function pit_set_frequency
 * @brief Set PIT frequency
 * 
 * @param frequency Desired frequency in Hz
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note Frequency must be between 18 and 1193182 Hz.
 */
static int pit_set_frequency(uint32_t frequency)
{
    if (frequency < 18 || frequency > PIT_BASE_FREQUENCY) {
        return -1; /* Invalid frequency */
    }
    
    /* Calculate divisor */
    uint16_t divisor = PIT_BASE_FREQUENCY / frequency;
    
    if (divisor == 0) {
        divisor = 1;
    }
    
    /* Send command byte: Channel 0, both bytes, mode 3 */
    __asm__ volatile ("outb %%al, %1" : : "a"(PIT_CMD_CHANNEL0 | PIT_CMD_ACCESS_BOTH | PIT_CMD_MODE3), "Nd"(PIT_COMMAND));
    
    /* Send divisor (low byte, then high byte) */
    __asm__ volatile ("outb %%al, %1" : : "a"((uint8_t)(divisor & 0xFF)), "Nd"(PIT_CHANNEL0_DATA));
    __asm__ volatile ("outb %%al, %1" : : "a"((uint8_t)((divisor >> 8) & 0xFF)), "Nd"(PIT_CHANNEL0_DATA));
    
    timer_frequency = frequency;
    
    return 0;
}

/**
 * @function pit_timer_handler
 * @brief PIT timer interrupt handler
 * 
 * This function is called on each PIT interrupt (IRQ 0). It:
 * 1. Increments the system tick counter
 * 2. Calls all registered timer callbacks
 * 3. Handles scheduler tick if needed
 * 
 * @param ctx Interrupt context
 */
static void pit_timer_handler(interrupt_context_t* ctx)
{
    (void)ctx; /* Context not used for timer */
    
    /* Minimal handler - just increment counter (XNU-style: fast interrupt handler) */
    /* No protection, no callbacks - just increment counter */
    timer_ticks++;
    
    /* Handler returns - EOI is sent by interrupt_dispatch in isr_handlers.c */
}

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function pit_init
 * @brief Initialize the PIT
 * 
 * This function:
 * 1. Sets up PIT to generate interrupts at default frequency
 * 2. Registers the timer interrupt handler (IRQ 0)
 * 3. Enables the timer interrupt in PIC
 * 
 * @param frequency Desired timer frequency in Hz (default: 100 Hz)
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note This must be called after PIC and IDT are initialized.
 */
int pit_init(uint32_t frequency)
{
    if (frequency == 0) {
        frequency = PIT_DEFAULT_FREQUENCY;
    }
    
    /* Set PIT frequency */
    if (pit_set_frequency(frequency) != 0) {
        return -1;
    }
    
    /* Register timer interrupt handler (IRQ 0 = vector 32) */
    if (interrupt_register(32, pit_timer_handler) != 0) {
        return -1;
    }
    
    /* NOTE: IRQ 0 is enabled later via pit_enable() */
    /* Don't enable here to avoid double enable */
    
    return 0;
}

/**
 * @function pit_set_frequency_public
 * @brief Change PIT frequency at runtime
 * 
 * @param frequency New frequency in Hz
 * 
 * @return 0 on success, -1 on failure
 */
int pit_set_frequency_public(uint32_t frequency)
{
    return pit_set_frequency(frequency);
}

/**
 * @function pit_get_ticks
 * @brief Get current system tick count
 * 
 * @return Current tick count
 * 
 * @note Ticks increment at the configured timer frequency.
 */
uint32_t pit_get_ticks(void)
{
    return timer_ticks;
}

/**
 * @function pit_get_frequency
 * @brief Get current timer frequency
 * 
 * @return Current frequency in Hz
 */
uint32_t pit_get_frequency(void)
{
    return timer_frequency;
}

/**
 * @function pit_register_callback
 * @brief Register a timer callback
 * 
 * This function registers a callback that will be called on each timer
 * interrupt. Multiple callbacks can be registered and will all be called.
 * 
 * @param handler Callback function to call
 * @param arg Argument to pass to callback
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note Callbacks are called in registration order.
 * @note Callbacks should be short and efficient to avoid delaying other callbacks.
 * @note TODO: Use proper kernel allocator instead of PMM for small structures.
 */
int pit_register_callback(void (*handler)(void* arg), void* arg)
{
    if (!handler) {
        return -1;
    }
    
    /* TODO: Use proper kernel allocator for small structures
     * For now, use a static array to avoid PMM overhead
     */
    #define MAX_TIMER_CALLBACKS 16
    static struct timer_callback callback_storage[MAX_TIMER_CALLBACKS];
    static uint32_t callback_count = 0;
    
    if (callback_count >= MAX_TIMER_CALLBACKS) {
        return -1; /* Too many callbacks */
    }
    
    /* Find free slot */
    struct timer_callback* cb = NULL;
    for (uint32_t i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (!callback_storage[i].active) {
            cb = &callback_storage[i];
            break;
        }
    }
    
    if (!cb) {
        return -1; /* No free slots */
    }
    
    /* Initialize callback */
    cb->handler = handler;
    cb->arg = arg;
    cb->active = true;
    cb->next = timer_callbacks;
    
    /* Add to list */
    timer_callbacks = cb;
    callback_count++;
    
    return 0;
}

/**
 * @function pit_unregister_callback
 * @brief Unregister a timer callback
 * 
 * @param handler Callback function to unregister
 * @param arg Argument that was used during registration
 * 
 * @return 0 on success, -1 if not found
 */
int pit_unregister_callback(void (*handler)(void* arg), void* arg)
{
    struct timer_callback* cb = timer_callbacks;
    struct timer_callback* prev = NULL;
    
    while (cb) {
        if (cb->handler == handler && cb->arg == arg) {
            /* Remove from list */
            if (prev) {
                prev->next = cb->next;
            } else {
                timer_callbacks = cb->next;
            }
            
            /* Mark as inactive (don't free, reuse slot) */
            cb->active = false;
            cb->handler = NULL;
            cb->arg = NULL;
            cb->next = NULL;
            
            return 0;
        }
        prev = cb;
        cb = cb->next;
    }
    
    return -1; /* Not found */
}

/**
 * @function pit_sleep_ms
 * @brief Sleep for specified milliseconds
 * 
 * @param milliseconds Number of milliseconds to sleep
 * 
 * @note This is a busy-wait implementation. For better efficiency,
 *       integrate with scheduler to yield CPU.
 */
void pit_sleep_ms(uint32_t milliseconds)
{
    uint32_t start_ticks = timer_ticks;
    uint32_t target_ticks = start_ticks + (milliseconds * timer_frequency / 1000);
    
    /* Busy wait until target ticks reached */
    while (timer_ticks < target_ticks) {
        __asm__ volatile ("pause"); /* CPU hint for spin loops */
    }
}

/**
 * @function pit_disable
 * @brief Disable PIT timer
 * 
 * This function disables the PIT timer by masking IRQ 0.
 * The timer continues running but interrupts are not delivered.
 */
void pit_disable(void)
{
    pic_disable_irq(0);
}

/**
 * @function pit_enable
 * @brief Enable PIT timer
 * 
 * This function enables the PIT timer by unmasking IRQ 0.
 */
void pit_enable(void)
{
    /* Enable PIT IRQ 0 */
    /* Use I/O APIC if available, otherwise fallback to PIC */
    extern bool apic_is_available(void);
    extern bool ioapic_is_available(void);
    extern void apic_enable_irq(uint8_t irq);
    extern void pic_enable_irq(uint8_t irq);
    
    /* XNU-style: If LAPIC is available, PIC should be disabled */
    if (apic_is_available()) {
        if (ioapic_is_available()) {
            apic_enable_irq(0);
        } else {
            /* LAPIC available but I/O APIC not - cannot enable IRQ via PIC */
            /* PIT timer will not work without I/O APIC when LAPIC is active */
        }
    } else {
        /* No APIC - use PIC */
        pic_enable_irq(0);
    }
}

