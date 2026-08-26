/**
 * @file apic.h
 * @brief APIC (Advanced Programmable Interrupt Controller) interface for x86_64
 * 
 * This module provides the interface for APIC initialization and management.
 * APIC is the modern interrupt controller for x86_64, replacing the legacy PIC.
 * 
 * @note This implementation is adapted for RodNIX.
 * @note APIC is preferred over PIC on modern systems.
 */

#ifndef _RODNIX_ARCH_X86_64_APIC_H
#define _RODNIX_ARCH_X86_64_APIC_H

#include <stdint.h>
#include <stdbool.h>

/* APIC detection and initialization */
bool apic_is_available(void);
bool ioapic_is_available(void);
int apic_init(void);
/* Per-CPU half of apic_init(), for a processor brought up after boot. */
int apic_init_ap(void);

/* APIC interrupt management */
void apic_enable(void);
void apic_disable(void);
void apic_send_eoi(void);
uint8_t apic_get_lapic_id(void);
/* Full-width LAPIC ID of the current CPU. In x2APIC mode the ID register is
 * 32 bits wide and not shifted, so the 8-bit accessor above cannot represent
 * it; use this one wherever an IPI destination or a topology key is needed. */
uint32_t apic_get_lapic_id_ext(void);

/* Inter-processor interrupts.
 *
 * `apic_id` is the target's LAPIC / x2APIC ID, not a kernel CPU index; see
 * interrupt_send_ipi() for the index-taking wrapper. All of these return 0 on
 * success and -1 if the LAPIC is unavailable or a previous IPI never left the
 * ICR. */
int apic_send_ipi(uint32_t apic_id, uint8_t vector);
int apic_send_ipi_self(uint8_t vector);
/* INIT and STARTUP are the AP wake-up sequence: INIT, a pause, then STARTUP
 * twice. `start_page` is the physical start address shifted right by 12, so
 * the AP begins executing at start_page * 4096 in real mode. */
int apic_send_init(uint32_t apic_id);
int apic_send_startup(uint32_t apic_id, uint8_t start_page);

/* Send this CPU an IPI and confirm it is delivered. Call with interrupts
 * masked; it opens a short window itself. Returns 0 if the vector arrived. */
int apic_ipi_selftest(void);

/* APIC timer (LAPIC timer) */
int apic_timer_init(uint32_t frequency);
void apic_timer_start(void);
void apic_timer_stop(void);
uint32_t apic_timer_get_ticks(void);
uint32_t apic_timer_get_frequency(void);
uint32_t apic_timer_get_lvt_raw(void);
uint32_t apic_timer_get_initial_count(void);
uint32_t apic_timer_get_current_count(void);

/* APIC IRQ management */
void apic_enable_irq(uint8_t irq);
void apic_enable_irq_level_low(uint8_t irq);
void apic_disable_irq(uint8_t irq);

#endif /* _RODNIX_ARCH_X86_64_APIC_H */
