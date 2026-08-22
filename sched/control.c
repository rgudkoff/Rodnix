#include "internal.h"
#include "../kernel/core/giant.h"
#include "../kernel/arch/percpu.h"
#include "../kernel/core/interrupts.h"
#include "../trace/tracev2.h"
#include "../trace/bootlog.h"
#include "../include/debug.h"

static void scheduler_exit_wake_joiner(thread_t* exiting)
{
    if (!exiting || !exiting->joiner) {
        return;
    }

    thread_t* joiner = exiting->joiner;
    exiting->joiner = NULL;
    if (joiner->state == THREAD_STATE_DEAD) {
        return;
    }

    /* Keep wake semantics deterministic for shell/run and POSIX exit paths. */
    joiner->priority = 220;
    joiner->base_priority = 220;
    joiner->dyn_priority = 220;
    joiner->inherited_priority = 220;
    joiner->has_inherited = 0;

    scheduler_wake(joiner);
}

static void scheduler_yield_internal(bool irq_context)
{
    if (!scheduler_running) {
        return;
    }
    resched_pending = true;

    if (irq_context || !thread_get_current()) {
        return;
    }

    /*
     * Holding a spinlock means preemption is off and switching is not
     * allowed; leave the request for the next tick, as before.
     */
    if (percpu_preempt_blocked()) {
        return;
    }

    /*
     * Otherwise, yield for real, and give up the kernel-wide lock while
     * doing it.
     *
     * This kernel waits by polling: pipes, waitpid, the tty, the sockets and
     * the reaper all spin on scheduler_yield() rather than sleeping on a
     * wait queue. Seventeen loops, and every one of them would hold Giant
     * across a wait for work that only another thread can do. Dropping it
     * here covers all of them, instead of depending on seventeen call sites
     * to remember -- and one that forgot would wedge the kernel outright.
     *
     * It also makes the name true. This used to set a flag and return, so a
     * caller "yielding" kept running until the next timer tick.
     */
    uint32_t giant_depth = giant_drop();
    interrupt_trigger_resched();
    giant_pickup(giant_depth);
}

void scheduler_yield(void)
{
    scheduler_yield_internal(false);
}

void scheduler_block(void)
{
    thread_t* cur = thread_get_current();
    if (!cur) {
        return;
    }

    if (in_scheduler) {
        return;
    }

    static int log_count = 0;
    if (bootlog_is_verbose() && log_count < 6) {
        kprintf("[SCHED] block tid=%llu state=%d\n",
                (unsigned long long)cur->thread_id,
                (int)cur->state);
        log_count++;
    }

    in_scheduler = true;

    /*
     * Somebody already woke this thread between it deciding to sleep and
     * getting here, so it is READY and sitting in the run queue. Blocking now
     * would put it back to sleep with the wakeup already spent -- and leave a
     * BLOCKED thread in the run queue, which is what surfaced as
     * "ready_dequeue: thread N state=3".
     *
     * A caller that reaches here is inside a loop re-testing its condition,
     * so returning without blocking is safe: it will look again and find the
     * condition satisfied.
     */
    if (cur->state != THREAD_STATE_RUNNING) {
        in_scheduler = false;
        return;
    }
    scheduler_thread_set_state(cur, THREAD_STATE_BLOCKED, "scheduler_block");
    tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_BLOCK,
                 cur->thread_id, cur->state);
    cur->last_sleep_tick = sched_ticks;
    if (bootlog_is_verbose() && log_count < 6) {
        kprintf("[SCHED] block set tid=%llu state=%d\n",
                (unsigned long long)cur->thread_id,
                (int)cur->state);
    }
    stats.blocked_tasks++;
    resched_pending = true;
    in_scheduler = false;
}

void scheduler_unblock(thread_t* thread)
{
    if (!thread) {
        return;
    }

    if (thread->state == THREAD_STATE_BLOCKED) {
        if (thread->sched_class == SCHED_CLASS_TIMESHARE) {
            uint64_t sleep_ticks = sched_ticks - thread->last_sleep_tick;
            if (sleep_ticks >= BOOST_THRESHOLD_TICKS) {
                int boost = (int)(sleep_ticks / BOOST_THRESHOLD_TICKS);
                if (boost > BOOST_MAX) {
                    boost = BOOST_MAX;
                }
                int base = thread->base_priority;
                int dyn = thread->dyn_priority + boost;
                thread->dyn_priority = clamp_dyn_priority(dyn, base);
            }
        }
        scheduler_thread_set_state(thread, THREAD_STATE_READY, "scheduler_unblock");
        if (stats.blocked_tasks > 0) {
            stats.blocked_tasks--;
        }
        ready_enqueue(thread);
    } else {
        DEBUG_WARN("unblock: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
    }
}

void scheduler_wake(thread_t* thread)
{
    if (!thread) {
        return;
    }
    if (thread->state == THREAD_STATE_DEAD) {
        return;
    }
    if (thread->state == THREAD_STATE_BLOCKED) {
        if (thread->sched_class == SCHED_CLASS_TIMESHARE) {
            uint64_t sleep_ticks = sched_ticks - thread->last_sleep_tick;
            if (sleep_ticks >= BOOST_THRESHOLD_TICKS) {
                int boost = (int)(sleep_ticks / BOOST_THRESHOLD_TICKS);
                if (boost > BOOST_MAX) {
                    boost = BOOST_MAX;
                }
                int base = thread->base_priority;
                int dyn = thread->dyn_priority + boost;
                thread->dyn_priority = clamp_dyn_priority(dyn, base);
            }
        }
        if (stats.blocked_tasks > 0) {
            stats.blocked_tasks--;
        }
        scheduler_thread_set_state(thread, THREAD_STATE_READY, "scheduler_wake_blocked");
        ready_enqueue(thread);
        resched_pending = true;
        return;
    }
    if (thread->state != THREAD_STATE_READY) {
        scheduler_thread_set_state(thread, THREAD_STATE_READY, "scheduler_wake_other");
        ready_enqueue(thread);
        resched_pending = true;
        return;
    }
    if (!ready_thread_is_queued(thread) && thread != thread_get_current()) {
        ready_enqueue(thread);
        resched_pending = true;
    }
}

void scheduler_exit_current(void)
{
    thread_t* cur = thread_get_current();
    if (!cur) {
        return;
    }

    /*
     * A thread that dies inside a system call still holds the kernel-wide
     * lock it took on entry, and will never reach the release at the other
     * end -- exit() does not return. Give it back here, where every path to
     * a dead thread passes, rather than at each of them.
     */
    (void)giant_drop();

    scheduler_exit_wake_joiner(cur);
    scheduler_thread_set_state(cur, THREAD_STATE_DEAD, "scheduler_exit_current");
    tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_EXIT,
                 cur->thread_id,
                 cur->task ? cur->task->task_id : 0);
    /*
     * Zombie the task only when this is the last live thread.
     * For multi-threaded processes, other threads continue running.
     */
    if (cur->task && cur->task->state != TASK_STATE_DEAD) {
        uint32_t live = 0;
        thread_t* t;
        TAILQ_FOREACH(t, &cur->task->threads, task_link) {
            if (t != cur && t->state != THREAD_STATE_DEAD) {
                live++;
            }
        }
        if (live == 0) {
            scheduler_task_set_state(cur->task, TASK_STATE_ZOMBIE, "scheduler_exit_current");
        }
    }
    resched_pending = true;
    interrupt_trigger_resched();
    for (;;) {
        cpu_idle();
    }
}

void scheduler_sleep(uint64_t milliseconds)
{
    if (!thread_get_current()) {
        return;
    }
    if (milliseconds == 0) {
        scheduler_yield();
        return;
    }
    (void)waitq_wait(&scheduler_sleep_waitq, milliseconds);
}

void scheduler_set_priority(thread_t* thread, uint8_t priority)
{
    if (!thread) {
        return;
    }

    thread_set_priority(thread, priority);
    thread->base_priority = priority;
    thread->dyn_priority = priority;
    if (thread->has_inherited) {
        if (thread->inherited_priority < thread->dyn_priority) {
            thread->inherited_priority = thread->dyn_priority;
        }
    }

    /* TODO: Re-insert thread into appropriate queue based on new priority */
}

void scheduler_inherit_priority(thread_t* target, const thread_t* donor)
{
    if (!target || !donor) {
        return;
    }
    int donor_prio = thread_effective_priority(donor);
    if (target->inherit_depth < 8) {
        target->inherit_stack[target->inherit_depth++] = target->inherited_priority;
    } else {
        target->has_inherit_overflow = 1;
        kprintf("[SCHED] priority inheritance overflow on thread %llu\n",
                (unsigned long long)target->thread_id);
    }
    if (!target->has_inherited || target->inherited_priority < donor_prio) {
        target->inherited_priority = donor_prio;
        target->has_inherited = 1;
    }
}

void scheduler_clear_inherit(thread_t* target)
{
    if (!target) {
        return;
    }
    if (target->has_inherit_overflow) {
        /* Stack was overflowed — restore to base priority conservatively */
        target->inherited_priority = target->base_priority;
        target->inherit_depth = 0;
        target->has_inherit_overflow = 0;
    } else if (target->inherit_depth > 0) {
        target->inherited_priority = target->inherit_stack[--target->inherit_depth];
    } else {
        target->inherited_priority = target->dyn_priority;
    }
    target->has_inherited = 0;
}

void scheduler_set_bucket(thread_t* thread, sched_bucket_t bucket)
{
    if (!thread) {
        return;
    }
    if ((int)bucket >= (int)SCHED_BUCKET_COUNT) {
        return;
    }
    thread->sched_bucket = (uint8_t)bucket;
    thread->sched_bucket_explicit = 1;
}
