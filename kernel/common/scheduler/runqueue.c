#include "internal.h"
#include "../../arch/x86_64/gdt.h"
#include "../../../include/debug.h"

void scheduler_update_tss(thread_t* thread)
{
    if (!thread || !thread->stack || thread->stack_size == 0) {
        return;
    }
    uint64_t rsp0 = (uint64_t)(uintptr_t)thread->stack + thread->stack_size - 16;
    tss_set_rsp0(rsp0);
}

int clamp_dyn_priority(int value, int base)
{
    int min = base - PENALTY_MAX;
    int max = base + BOOST_MAX;
    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    return value;
}

int thread_effective_priority(const thread_t* thread)
{
    if (!thread) {
        return SCHEDULER_DEFAULT_PRIORITY;
    }
    if (thread->base_priority == 0 && thread->priority != 0) {
        return thread->priority;
    }
    int dyn = (thread->dyn_priority >= 0) ? thread->dyn_priority : 0;
    if (thread->has_inherited) {
        if (thread->inherited_priority > dyn) {
            return thread->inherited_priority;
        }
    }
    return dyn;
}

void scheduler_reset_timeslice(const thread_t* thread)
{
    if (!thread) {
        ticks_until_preempt = ticks_per_slice;
        return;
    }
    if (thread->sched_class == SCHED_CLASS_REALTIME) {
        ticks_until_preempt = REALTIME_QUANTUM_TICKS;
        return;
    }
    int prio = thread_effective_priority(thread);
    uint32_t base = ticks_per_slice;
    if (base == 0) {
        base = 1;
    }
    uint32_t prio_extra = (uint32_t)(prio / TIMESHARE_PRIO_STEP);
    if (prio_extra > TIMESHARE_MAX_BONUS) {
        prio_extra = TIMESHARE_MAX_BONUS;
    }
    uint32_t usage_extra = 0;
    if (thread->sched_usage > CPU_BOUND_THRESHOLD) {
        usage_extra = CPU_BOUND_EXTRA_TICKS;
    }
    ticks_until_preempt = base + prio_extra + usage_extra;
    if (ticks_until_preempt == 0) {
        ticks_until_preempt = 1;
    }
}

bool ready_thread_is_queued(const thread_t* thread)
{
    return thread && thread->sched_link.tqe_prev != NULL;
}

void ready_enqueue(thread_t* thread)
{
    if (!thread) {
        return;
    }

    if (ready_thread_is_queued(thread)) {
        DEBUG_WARN("ready_enqueue: thread %llu already queued", (unsigned long long)thread->thread_id);
        return;
    }
    int q = ready_queue_index_for_thread(thread);
    if (q < 0 || q >= READY_QUEUE_LEVELS) {
        q = READY_QUEUE_LEVELS - 1;
    }
    TAILQ_INSERT_TAIL(&ready_queues[q], thread, sched_link);
    stats.ready_tasks++;
}

thread_t* ready_dequeue(void)
{
    int start = READY_QUEUE_LEVELS - 1;
    int end = 0;

    if (current_policy == SCHED_POLICY_RR || current_policy == SCHED_POLICY_FIFO) {
        start = 1;
        end = 1;
    }

    if (start == end) {
        int q = start;
        struct ready_queue_head* queue = &ready_queues[q];
        thread_t* thread = TAILQ_FIRST(queue);
        if (!thread) {
            return NULL;
        }

        if (thread->state != THREAD_STATE_READY) {
            DEBUG_WARN("ready_dequeue: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
        }
        TAILQ_REMOVE(queue, thread, sched_link);
        if (stats.ready_tasks > 0) {
            stats.ready_tasks--;
        }
        return thread;
    }

    for (int q = start; q >= end; q--) {
        struct ready_queue_head* queue = &ready_queues[q];
        thread_t* thread = TAILQ_FIRST(queue);
        if (!thread) {
            continue;
        }

        if (thread->state != THREAD_STATE_READY) {
            DEBUG_WARN("ready_dequeue: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
        }
        TAILQ_REMOVE(queue, thread, sched_link);
        if (stats.ready_tasks > 0) {
            stats.ready_tasks--;
        }
        return thread;
    }

    return NULL;
}

int ready_queue_index_for_thread(const thread_t* thread)
{
    if (!thread) {
        return READY_QUEUE_LEVELS - 1;
    }

    if (current_policy == SCHED_POLICY_RR || current_policy == SCHED_POLICY_FIFO) {
        return 1;
    }

    /* Priority bands: 0-63 (low), 64-191 (normal), 192-255 (high) */
    int prio = thread_effective_priority(thread);
    if (prio >= 192) {
        return 2;
    }
    if (prio >= 64) {
        return 1;
    }
    return 0;
}
