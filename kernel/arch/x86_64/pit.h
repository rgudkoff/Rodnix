/**
 * @file pit.h
 * @brief PIT (Programmable Interval Timer) interface
 */

#ifndef _RODNIX_ARCH_X86_64_PIT_H
#define _RODNIX_ARCH_X86_64_PIT_H

#include <stdint.h>

/* Initialize PIT */
int pit_init(uint32_t frequency);

/* Frequency control */
int pit_set_frequency_public(uint32_t frequency);
uint32_t pit_get_frequency(void);

/* Timer information */
uint32_t pit_get_ticks(void);

/* Callback registration */
int pit_register_callback(void (*handler)(void* arg), void* arg);
int pit_unregister_callback(void (*handler)(void* arg), void* arg);

/* Sleep function */
void pit_sleep_ms(uint32_t milliseconds);

/* Enable/disable */
void pit_enable(void);
void pit_disable(void);

#endif /* _RODNIX_ARCH_X86_64_PIT_H */

