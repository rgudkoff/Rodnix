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
    /* Holder's CPU index plus one, or zero when free. Named in the panic on
     * a same-CPU recursive acquire -- which the old cli-based "lock" silently
     * tolerated -- and in the one on a spin that never ends, where knowing
     * which processor is sitting on the lock is most of the answer. */
    volatile uint32_t owner_plus_one;
    /* Witness node for this lock, resolved on first acquire and cached here
     * so the common path costs a load rather than a name lookup. Zero means
     * not yet resolved; it is never cleared, so a re-init keeps its identity. */
    uint32_t witness_id;
} spinlock_t;

void spinlock_init(spinlock_t* lock);

/*
 * Every acquire carries the lock's name and its call site. The name was
 * already needed for the recursion panic; the site is what turns a lock-order
 * report from "these two locks disagree" into two file:line pairs to go and
 * read. Both are string literals, so this costs nothing at runtime beyond two
 * register loads on the path that reports.
 */
void spinlock_lock_named(spinlock_t* lock, const char* name,
                         const char* file, int line);
#define spinlock_lock(lock) \
    spinlock_lock_named((lock), #lock, __FILE__, __LINE__)

void spinlock_unlock(spinlock_t* lock);

bool spinlock_trylock_named(spinlock_t* lock, const char* name,
                            const char* file, int line);
#define spinlock_trylock(lock) \
    spinlock_trylock_named((lock), #lock, __FILE__, __LINE__)

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
uint64_t spinlock_lock_irqsave_named(spinlock_t* lock, const char* name,
                                     const char* file, int line);
#define spinlock_lock_irqsave(lock) \
    spinlock_lock_irqsave_named((lock), #lock, __FILE__, __LINE__)

void spinlock_unlock_irqrestore(spinlock_t* lock, uint64_t flags);

#endif /* _RODNIX_FABRIC_SPIN_H */
