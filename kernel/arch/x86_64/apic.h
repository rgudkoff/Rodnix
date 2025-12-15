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

/* APIC interrupt management */
void apic_enable(void);
void apic_disable(void);
void apic_send_eoi(void);
uint8_t apic_get_lapic_id(void);

/* APIC timer (LAPIC timer) */
int apic_timer_init(uint32_t frequency);
void apic_timer_start(void);
void apic_timer_stop(void);
uint32_t apic_timer_get_ticks(void);
uint32_t apic_timer_get_frequency(void);

/* APIC IRQ management */
void apic_enable_irq(uint8_t irq);
void apic_disable_irq(uint8_t irq);

#endif /* _RODNIX_ARCH_X86_64_APIC_H */

