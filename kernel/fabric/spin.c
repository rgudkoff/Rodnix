/**
 * @file spin.c
 * @brief Simple spinlock implementation for Fabric
 */

#include "spin.h"
#include "../core/cpu.h"
#include "../core/witness.h"
#include "../core/hygiene.h"
#include "../arch/percpu.h"
#include "../../include/debug.h"
#include "../../include/console.h"

/*
 * How long a spin may last before it is called a deadlock rather than
 * contention.
 *
 * A spinlock here protects tens of instructions; the longest legitimate hold
 * in this kernel is a filesystem metadata update, still microseconds. One
 * second is six orders of magnitude of headroom, so a spin that reaches it is
 * not slow, it is stuck.
 *
 * It is a second of *time*, from the TSC, and not a count of pause
 * instructions. The first version of this counted pauses, which was wrong in
 * two ways worth naming: a pause costs a different number of cycles under
 * emulation than on hardware, so the "seconds of headroom" in its comment
 * were unfounded; and an interrupt landing mid-spin was charged to the lock,
 * so a busy machine could trip a threshold that a quiet one would not. XNU
 * makes the same distinction explicitly -- its spin state carries hwss_irq_*
 * alongside the deadline, and its hygiene paths re-check the net duration
 * before panicking. This does both.
 *
 * The whole thing exists because a deadlock is the one failure that produces
 * no output at all, which makes it the one most worth spending a counter on.
 * FreeBSD reaches the same conclusion in _mtx_lock_spin_failed.
 */
#define SPIN_TIMEOUT_US       1000000ULL
/* Backstop for the window before the TSC is calibrated, where a deadline
 * cannot be computed at all. Deliberately huge: its job is to stop an infinite
 * hang, not to measure anything. */
#define SPIN_TIMEOUT_PAUSES   4000000000ULL
/* rdtsc is not free, so the deadline is only consulted every so many spins.
 * An uncontended acquire never reaches the check at all. */
#define SPIN_CHECK_MASK       0xFFFu
/*
 * A deadline alone is not enough to declare a lock stuck, because the clock
 * can move while the processor does not: under emulation the host may
 * deschedule the whole guest, and the TSC counts through it. Measured -- a
 * twelve second gap in which the timer tick advanced by zero. A spin that has
 * genuinely lasted a second has executed far more pause instructions than
 * this; one that only looks like it has executed almost none. Both conditions
 * must hold.
 */
#define SPIN_MIN_SPINS        10000000ULL

#define SPIN_NO_DEADLINE      0xFFFFFFFFFFFFFFFFULL

static uint64_t g_spin_timeout_ticks;

static uint64_t spin_timeout_ticks(void)
{
    uint64_t t = g_spin_timeout_ticks;
    if (t == 0) {
        uint64_t hz = cpu_get_frequency();
        /* Resolved lazily rather than from an init hook: the first contended
         * spin is long after calibration, and a lock that is taken before it
         * still gets the backstop. */
        t = hz ? ((hz / 1000000ULL) * SPIN_TIMEOUT_US) : SPIN_NO_DEADLINE;
        g_spin_timeout_ticks = t;
    }
    return t;
}

/* State of one waiting session. Nothing is sampled until the spin has already
 * gone on long enough to be worth timing. */
struct spin_wait {
    uint64_t spins;
    uint64_t started;    /* TSC at the first check; 0 until then */
    uint64_t irq_base;   /* handler time on this CPU at that moment */
};

__attribute__((noreturn))
static void spin_timeout(spinlock_t* lock, const char* name,
                         const char* file, int line,
                         uint64_t gross, uint64_t net)
{
    uint32_t owner = lock->owner_plus_one;
    uint64_t hz = cpu_get_frequency();
    uint64_t us = hz ? ((net * 1000000ULL) / hz) : 0;
    uint64_t irq_us = hz ? (((gross - net) * 1000000ULL) / hz) : 0;

    kprintf("\n[SPIN] cpu%u stuck on %s (%s:%d)\n",
            (unsigned)cpu_get_id(), name ? name : "?", file, line);
    if (hz) {
        kprintf("[SPIN] waiting %lluus, of which %lluus was interrupt time\n",
                (unsigned long long)us, (unsigned long long)irq_us);
    } else {
        kprintf("[SPIN] TSC not calibrated -- pause-count backstop fired\n");
    }
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

/* One iteration of waiting. Returns having either paused, or panicked. */
static inline void spin_wait_step(struct spin_wait* w, spinlock_t* lock,
                                  const char* name, const char* file, int line)
{
    __asm__ volatile ("pause");

    if ((++w->spins & SPIN_CHECK_MASK) != 0) {
        return;
    }

    if (w->started == 0) {
        w->started = cpu_get_time();
        w->irq_base = hygiene_irq_ticks();
        return;
    }

    uint64_t limit = spin_timeout_ticks();
    if (limit != SPIN_NO_DEADLINE) {
        uint64_t gross = cpu_get_time() - w->started;
        if (gross >= limit) {
            /* Second check on the net duration, so an interrupt storm during
             * the wait is not reported as a stuck lock. */
            uint64_t irq = hygiene_irq_ticks() - w->irq_base;
            uint64_t net = (gross > irq) ? (gross - irq) : 0;
            if (net >= limit && w->spins >= SPIN_MIN_SPINS) {
                spin_timeout(lock, name, file, line, gross, net);
            }
        }
    }

    if (w->spins >= SPIN_TIMEOUT_PAUSES) {
        spin_timeout(lock, name, file, line, 0, 0);
    }
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

    struct spin_wait w = { 0, 0, 0 };
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        spin_wait_step(&w, lock, name, file, line);
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


/* IF in RFLAGS. Whether it was set before the cli is what says this is the
 * outermost mask -- the one that actually starts a window -- rather than a
 * nested one that extends nothing. */
#define RFLAGS_IF (1ULL << 9)

uint64_t spinlock_lock_irqsave_named(spinlock_t* lock, const char* name,
                                     const char* file, int line)
{
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) :: "memory");

    if ((flags & RFLAGS_IF) && hygiene_enabled()) {
        hygiene_int_begin(file, line);
    }

    if (lock) {
        uint32_t me = cpu_get_id() + 1u;
        if (lock->owner_plus_one == me) {
            panicf("spinlock %s re-acquired on cpu%u",
                   name ? name : "?", (unsigned)(me - 1u));
        }

        (void)witness_check(&lock->witness_id, name, file, line);

        struct spin_wait w = { 0, 0, 0 };
        while (__sync_lock_test_and_set(&lock->locked, 1)) {
            spin_wait_step(&w, lock, name, file, line);
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
    /* Closed before the flags go back, so the measurement covers the whole
     * masked region and the reporting inside it does not run with interrupts
     * unexpectedly on. */
    if ((flags & RFLAGS_IF) && hygiene_enabled()) {
        hygiene_int_end();
    }
    __asm__ volatile ("pushq %0\n\tpopfq" :: "r"(flags) : "memory", "cc");
}
