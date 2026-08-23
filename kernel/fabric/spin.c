/**
 * @file spin.c
 * @brief Simple spinlock implementation for Fabric
 */

#include "spin.h"
#include "../core/cpu.h"
#include "../core/witness.h"
#include "../arch/percpu.h"
#include "../../include/debug.h"
#include "../../include/console.h"

/*
 * How long a spin may last before it is called a deadlock rather than
 * contention.
 *
 * A spinlock here protects tens of instructions; the longest legitimate hold
 * in this kernel is a filesystem metadata update, still microseconds. Four
 * hundred million pauses is seconds even under emulation -- four orders of
 * magnitude of headroom -- so a spin that reaches it is not slow, it is
 * stuck.
 *
 * This exists because the alternative is what we have been debugging: a
 * machine that stops with nothing on the wire. A deadlock is the one failure
 * that produces no output at all, which makes it the one most worth spending
 * a counter on. FreeBSD reaches the same conclusion in _mtx_lock_spin_failed.
 */
#define SPIN_TIMEOUT_PAUSES 400000000ULL

__attribute__((noreturn))
static void spin_timeout(spinlock_t* lock, const char* name,
                         const char* file, int line)
{
    uint32_t owner = lock->owner_plus_one;
    kprintf("\n[SPIN] cpu%u stuck on %s (%s:%d)\n",
            (unsigned)cpu_get_id(), name ? name : "?", file, line);
    if (owner) {
        kprintf("[SPIN] held by cpu%u\n", (unsigned)(owner - 1u));
    } else {
        kprintf("[SPIN] lock reads free -- released without waking this spin\n");
    }
    witness_dump_held();
    witness_dump_graph();
    panicf("spinlock %s: spin timeout on cpu%u",
           name ? name : "?", (unsigned)cpu_get_id());
}

void spinlock_init(spinlock_t* lock)
{
    if (!lock) {
        return;
    }
    lock->locked = 0;
    lock->owner_plus_one = 0;
    /* witness_id is deliberately not cleared: the node it names describes the
     * lock's identity in the order graph, and re-initialising the lock does
     * not make it a different lock. */
    __asm__ volatile ("" ::: "memory");
}

void spinlock_lock_named(spinlock_t* lock, const char* name,
                         const char* file, int line)
{
    if (!lock) {
        return;
    }

    /* Preemption off for the whole hold, interrupts left alone. This is the
     * variant for long critical sections -- filesystem work, block I/O --
     * where masking interrupts throughout would wreck latency, but being
     * preempted while holding the lock would wedge the next thread on this
     * processor that wants it.
     *
     * It also pins this processor, which is what lets witness keep its
     * held-lock set per-CPU and unlocked. */
    percpu_preempt_disable();

    /* Same self-deadlock check as the irqsave variant. A recursive acquire
     * is a hang with no output otherwise, and every lock converted from the
     * old cli-based idiom is a candidate for it. */
    uint32_t me = cpu_get_id() + 1u;
    if (lock->owner_plus_one == me) {
        panicf("spinlock %s re-acquired on cpu%u",
               name ? name : "?", (unsigned)(me - 1u));
    }

    /* Before the acquire, so a reversal is reported rather than entered. */
    (void)witness_check(&lock->witness_id, name, file, line);

    uint64_t spins = 0;
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        __asm__ volatile ("pause");
        if (++spins == SPIN_TIMEOUT_PAUSES) {
            spin_timeout(lock, name, file, line);
        }
    }
    lock->owner_plus_one = me;
    witness_acquired(&lock->witness_id, lock, name, file, line);
    __asm__ volatile ("" ::: "memory");
}

void spinlock_unlock(spinlock_t* lock)
{
    if (!lock) {
        return;
    }

    __asm__ volatile ("" ::: "memory");
    witness_release(lock);
    lock->owner_plus_one = 0;
    __sync_lock_release(&lock->locked);
    percpu_preempt_enable();
}

bool spinlock_trylock_named(spinlock_t* lock, const char* name,
                            const char* file, int line)
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
        /* Recorded as held but contributing no order: a try never waits, so
         * it cannot be half of a cycle, and treating it as one would report
         * reversals that cannot happen. */
        witness_acquired(&lock->witness_id, lock, name, file, line);
        return true;
    }
    percpu_preempt_enable();
    return false;
}


uint64_t spinlock_lock_irqsave_named(spinlock_t* lock, const char* name,
                                     const char* file, int line)
{
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) :: "memory");

    if (lock) {
        uint32_t me = cpu_get_id() + 1u;
        if (lock->owner_plus_one == me) {
            panicf("spinlock %s re-acquired on cpu%u",
                   name ? name : "?", (unsigned)(me - 1u));
        }

        (void)witness_check(&lock->witness_id, name, file, line);

        uint64_t spins = 0;
        while (__sync_lock_test_and_set(&lock->locked, 1)) {
            __asm__ volatile ("pause");
            if (++spins == SPIN_TIMEOUT_PAUSES) {
                spin_timeout(lock, name, file, line);
            }
        }
        lock->owner_plus_one = me;
        witness_acquired(&lock->witness_id, lock, name, file, line);
    }
    __asm__ volatile ("" ::: "memory");
    return flags;
}

void spinlock_unlock_irqrestore(spinlock_t* lock, uint64_t flags)
{
    __asm__ volatile ("" ::: "memory");
    if (lock) {
        witness_release(lock);
        lock->owner_plus_one = 0;
        __sync_lock_release(&lock->locked);
    }
    __asm__ volatile ("pushq %0\n\tpopfq" :: "r"(flags) : "memory", "cc");
}
