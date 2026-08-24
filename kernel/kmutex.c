/**
 * @file kmutex.c
 * @brief Sleeping mutex, built on the wait queues.
 */

#include "core/kmutex.h"
#include "../sched/scheduler.h"
#include "../sched/waitq.h"
#include "arch/percpu.h"
#include "../include/debug.h"
#include <stddef.h>

void kmutex_init(kmutex_t* m, const char* name)
{
    if (!m) {
        return;
    }
    spinlock_init(&m->guard);
    m->owner = NULL;
    m->depth = 0;
    m->recursive = false;
    m->name = name ? name : "kmutex";
    waitq_init(&m->waiters, m->name);
}

void kmutex_init_recursive(kmutex_t* m, const char* name)
{
    kmutex_init(m, name);
    if (m) {
        m->recursive = true;
    }
}

static void kmutex_check_context(const kmutex_t* m)
{
    /* Holding a spinlock means preemption is off; sleeping now would keep it
     * off for the duration of the sleep and stop every processor waiting for
     * that spinlock. Catch it here, where the name of the mutex is still
     * known, rather than as a hang later. */
    if (percpu_preempt_blocked()) {
        panicf("kmutex %s acquired while holding a spinlock",
               m->name ? m->name : "?");
    }
}

void kmutex_lock(kmutex_t* m)
{
    if (!m) {
        return;
    }

    /*
     * Before the scheduler runs there is one thread of control and nothing
     * to put on a wait queue. Locking is then a no-op rather than an error:
     * early boot legitimately touches subsystems that will later be
     * mutex-protected, and refusing here would mean two versions of every
     * caller. The depth is still tracked so unlock stays symmetric.
     */
    thread_t* self = thread_get_current();
    if (!self) {
        m->depth++;
        return;
    }

    kmutex_check_context(m);

    for (;;) {
        uint64_t f = spinlock_lock_irqsave(&m->guard);

        if (m->owner == NULL) {
            m->owner = self;
            m->depth = 1;
            spinlock_unlock_irqrestore(&m->guard, f);
            return;
        }

        if (m->owner == self) {
            if (!m->recursive) {
                spinlock_unlock_irqrestore(&m->guard, f);
                panicf("kmutex %s re-acquired by its owner",
                       m->name ? m->name : "?");
            }
            m->depth++;
            spinlock_unlock_irqrestore(&m->guard, f);
            return;
        }

        /*
         * Enqueue before dropping the guard. That ordering is what closes the
         * lost-wakeup window: an unlock that runs between here and the sleep
         * below finds this thread already on the queue and takes it off, and
         * waitq_wait_until then sees it is no longer queued and returns
         * without ever blocking.
         */
        (void)waitq_enqueue(&m->waiters, self);
        spinlock_unlock_irqrestore(&m->guard, f);

        (void)waitq_wait_until(&m->waiters, 0);
        /* Woken means the lock was free at that moment, not that it still
         * is -- another processor may have taken it in between. Round again. */
    }
}

void kmutex_unlock(kmutex_t* m)
{
    if (!m) {
        return;
    }

    thread_t* self = thread_get_current();
    if (!self) {
        if (m->depth > 0) {
            m->depth--;
        }
        return;
    }

    uint64_t f = spinlock_lock_irqsave(&m->guard);

    if (m->owner != self) {
        spinlock_unlock_irqrestore(&m->guard, f);
        panicf("kmutex %s released by a thread that does not hold it",
               m->name ? m->name : "?");
    }

    if (--m->depth > 0) {
        spinlock_unlock_irqrestore(&m->guard, f);
        return;
    }

    m->owner = NULL;
    spinlock_unlock_irqrestore(&m->guard, f);

    /* Woken outside the guard: no reason to fix an order between this lock
     * and the waitq's. Losing the race to a fresh unlock is fine -- the
     * TH_WAIT interlock means a waiter that has not blocked yet simply sees
     * itself off the queue and retries; removal and wakeup are one critical
     * section inside waitq_wake_one(). */
    (void)waitq_wake_one(&m->waiters);
}

bool kmutex_trylock(kmutex_t* m)
{
    if (!m) {
        return false;
    }

    thread_t* self = thread_get_current();
    uint64_t f = spinlock_lock_irqsave(&m->guard);

    if (m->owner == NULL) {
        m->owner = self;
        m->depth = 1;
        spinlock_unlock_irqrestore(&m->guard, f);
        return true;
    }
    if (m->owner == self && m->recursive) {
        m->depth++;
        spinlock_unlock_irqrestore(&m->guard, f);
        return true;
    }

    spinlock_unlock_irqrestore(&m->guard, f);
    return false;
}

bool kmutex_owned(const kmutex_t* m)
{
    if (!m) {
        return false;
    }
    thread_t* self = thread_get_current();
    return self != NULL && m->owner == self;
}
