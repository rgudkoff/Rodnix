/**
 * @file irqstat.h
 * @brief Per-CPU interrupt counters.
 *
 * Counted per processor and per vector, because on a multiprocessor machine
 * the useful question is not "how many" but "where". A single total cannot
 * tell an interrupt storm on one core from even load across all of them, and
 * that distinction is the first thing worth looking at when routing goes
 * wrong.
 *
 * Each processor writes only its own row, so the increment needs no atomic
 * and no lock -- which matters, since it sits in the dispatcher on every
 * interrupt taken. Readers may see a row mid-update; for statistics that is
 * the right trade.
 */

#ifndef _RODNIX_ARCH_X86_64_IRQSTAT_H
#define _RODNIX_ARCH_X86_64_IRQSTAT_H

#include <stdbool.h>
#include <stdint.h>

/* Called from the interrupt dispatcher. `handled` is false when no handler
 * was registered for the vector -- worth separating, because an unhandled
 * interrupt is a configuration problem rather than traffic. */
void irqstat_count(uint32_t cpu, uint32_t vector, bool handled);

/* Times this vector fired on this processor. */
uint64_t irqstat_get(uint32_t cpu, uint32_t vector);

/* Times this vector fired on any processor. */
uint64_t irqstat_get_total(uint32_t vector);

/* Interrupts on this processor with no registered handler. */
uint64_t irqstat_get_unhandled(uint32_t cpu);

/* True if this vector has ever fired anywhere -- lets a listing skip the
 * 250-odd vectors that never do. */
bool irqstat_vector_seen(uint32_t vector);

/* Consecutive firings of this vector with no handler to take them.
 * Consecutive is the point: a device that strays a few times before its
 * driver registers resets this to zero the moment the driver runs, while a
 * line stuck asserted never does. That distinction is what lets a runaway
 * line be shut off without punishing a slow one. */
uint64_t irqstat_unhandled_streak(uint32_t vector);

/* Forget the streak for a vector -- called when a handler is registered, so a
 * driver that arrives late starts from a clean slate. */
void irqstat_clear_streak(uint32_t vector);

#endif /* _RODNIX_ARCH_X86_64_IRQSTAT_H */
