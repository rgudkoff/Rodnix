/**
 * @file giant.h
 * @brief The kernel-wide lock.
 *
 * One recursive sleeping mutex covering every subsystem that has not been
 * made safe on its own. Thread-context kernel code holds it; interrupt
 * handlers do not, and keep their spinlocks.
 *
 * The point is the direction of travel. Locking bottom-up, subsystem by
 * subsystem, leaves the kernel incorrect until the last one is done and
 * offers no way to know how many remain -- you find them by being crashed.
 * Starting from one lock over everything gives correctness immediately and
 * turns the rest into performance work whose size is visible. FreeBSD's
 * mutex_init() ends with mtx_lock(&Giant) for exactly this reason.
 *
 * Recursive because a lock covering this much ground cannot avoid being
 * re-entered, which is why FreeBSD declares Giant MTX_RECURSE.
 *
 * Held across:
 *   - a system call, from dispatch to return
 *   - an exception taken in thread context
 *   - a kernel thread's whole life, apart from the idle thread
 *
 * Dropped for:
 *   - the return to ring 3, where the thread runs no kernel code
 *   - any sleep -- see giant_drop()
 */

#ifndef _RODNIX_CORE_GIANT_H
#define _RODNIX_CORE_GIANT_H

#include "kmutex.h"
#include <stdint.h>

extern kmutex_t giant;

void giant_init(void);

static inline void giant_lock(void)
{
    kmutex_lock(&giant);
}

static inline void giant_unlock(void)
{
    kmutex_unlock(&giant);
}

static inline bool giant_owned(void)
{
    return kmutex_owned(&giant);
}

/*
 * Release Giant entirely, whatever the recursion depth, and report it.
 *
 * A thread that sleeps holding the kernel-wide lock stops the kernel for as
 * long as it sleeps, so every blocking point drops it first and takes it back
 * on the way out. This is FreeBSD's DROP_GIANT/PICKUP_GIANT pair, and the
 * rule WITNESS states as "Giant must be released when blocking on a sleepable
 * lock".
 *
 * Returns 0 when the caller did not hold it, which makes the pair safe to
 * use unconditionally at a blocking point.
 */
uint32_t giant_drop(void);
void giant_pickup(uint32_t depth);

#endif /* _RODNIX_CORE_GIANT_H */
