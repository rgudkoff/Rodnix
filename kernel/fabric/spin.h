/**
 * @file spin.h
 * @brief Simple spinlock implementation for Fabric
 */

#ifndef _RODNIX_FABRIC_SPIN_H
#define _RODNIX_FABRIC_SPIN_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t locked;
    /* Holder's CPU index plus one, or zero when free. Only meaningful for
     * the irqsave variants, and only there to turn a same-CPU recursive
     * acquire -- which the old cli-based "lock" silently tolerated -- into a
     * panic naming the lock instead of a hang. */
    volatile uint32_t owner_plus_one;
} spinlock_t;

void spinlock_init(spinlock_t* lock);
void spinlock_lock(spinlock_t* lock);
void spinlock_unlock(spinlock_t* lock);
bool spinlock_trylock(spinlock_t* lock);

/*
 * Acquire with interrupts masked on this processor, returning the previous
 * RFLAGS so the caller can restore whatever it had.
 *
 * Both halves are needed and for different reasons. The spin excludes other
 * processors; masking excludes an interrupt handler on *this* processor that
 * would take the same lock and then spin forever waiting for the interrupted
 * code to release it. Masking alone -- which is what a cli-based "lock"
 * amounts to -- excludes nothing at all once a second processor exists.
 *
 * Interrupts stay masked for the whole hold, including while spinning, for
 * the same deadlock reason.
 */
uint64_t spinlock_lock_irqsave_named(spinlock_t* lock, const char* name);
#define spinlock_lock_irqsave(lock) spinlock_lock_irqsave_named((lock), #lock)
void spinlock_unlock_irqrestore(spinlock_t* lock, uint64_t flags);

#endif /* _RODNIX_FABRIC_SPIN_H */

