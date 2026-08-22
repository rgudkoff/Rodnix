/**
 * @file spin.c
 * @brief Simple spinlock implementation for Fabric
 */

#include "spin.h"
#include "../core/cpu.h"
#include "../arch/percpu.h"
#include "../../include/debug.h"

void spinlock_init(spinlock_t* lock)
{
    if (!lock) {
        return;
    }
    lock->locked = 0;
    __asm__ volatile ("" ::: "memory");
}

void spinlock_lock(spinlock_t* lock)
{
    if (!lock) {
        return;
    }

    /* Preemption off for the whole hold, interrupts left alone. This is the
     * variant for long critical sections -- filesystem work, block I/O --
     * where masking interrupts throughout would wreck latency, but being
     * preempted while holding the lock would wedge the next thread on this
     * processor that wants it. */
    percpu_preempt_disable();

    /* Same self-deadlock check as the irqsave variant. A recursive acquire
     * is a hang with no output otherwise, and every lock converted from the
     * old cli-based idiom is a candidate for it. */
    uint32_t me = cpu_get_id() + 1u;
    if (lock->owner_plus_one == me) {
        panicf("spinlock re-acquired on cpu%u", (unsigned)(me - 1u));
    }

    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        __asm__ volatile ("pause");
    }
    lock->owner_plus_one = me;
    __asm__ volatile ("" ::: "memory");
}

void spinlock_unlock(spinlock_t* lock)
{
    if (!lock) {
        return;
    }

    __asm__ volatile ("" ::: "memory");
    lock->owner_plus_one = 0;
    __sync_lock_release(&lock->locked);
    percpu_preempt_enable();
}

bool spinlock_trylock(spinlock_t* lock)
{
    if (!lock) {
        return false;
    }

    /* Same contract as spinlock_lock() on success, so the caller can release
     * it with spinlock_unlock(): preemption stays off only if the lock was
     * actually taken. */
    percpu_preempt_disable();
    if (__sync_lock_test_and_set(&lock->locked, 1) == 0) {
        lock->owner_plus_one = cpu_get_id() + 1u;
        return true;
    }
    percpu_preempt_enable();
    return false;
}


uint64_t spinlock_lock_irqsave_named(spinlock_t* lock, const char* name)
{
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) :: "memory");

    if (lock) {
        uint32_t me = cpu_get_id() + 1u;
        if (lock->owner_plus_one == me) {
            panicf("spinlock %s re-acquired on cpu%u",
                   name ? name : "?", (unsigned)(me - 1u));
        }
        while (__sync_lock_test_and_set(&lock->locked, 1)) {
            __asm__ volatile ("pause");
        }
        lock->owner_plus_one = me;
    }
    __asm__ volatile ("" ::: "memory");
    return flags;
}

void spinlock_unlock_irqrestore(spinlock_t* lock, uint64_t flags)
{
    __asm__ volatile ("" ::: "memory");
    if (lock) {
        lock->owner_plus_one = 0;
        __sync_lock_release(&lock->locked);
    }
    __asm__ volatile ("pushq %0\n\tpopfq" :: "r"(flags) : "memory", "cc");
}
