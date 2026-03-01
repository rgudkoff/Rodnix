#include "internal.h"

void scheduler_tick(void)
{
    if (!scheduler_running) {
        return;
    }

    sched_ticks++;
    waitq_tick(sched_ticks);
    if (current_thread && current_thread->state == THREAD_STATE_RUNNING) {
        current_thread->sched_usage = (current_thread->sched_usage * 7) / 8;
        current_thread->sched_usage++;
        if (current_thread->sched_class == SCHED_CLASS_TIMESHARE) {
            if ((current_thread->sched_usage % PENALTY_STEP_TICKS) == 0) {
                int base = current_thread->base_priority;
                int dyn = current_thread->dyn_priority - 1;
                current_thread->dyn_priority = clamp_dyn_priority(dyn, base);
            }
        }
    }

    if (ticks_until_preempt > 0) {
        ticks_until_preempt--;
    }

    if (ticks_until_preempt == 0) {
        ticks_until_preempt = ticks_per_slice;
        resched_pending = true;
    }
}

void scheduler_set_tick_rate(uint32_t hz)
{
    if (hz == 0) {
        return;
    }

    uint32_t ticks = (hz * SCHEDULER_TIME_SLICE_MS + 999) / 1000;
    if (ticks == 0) {
        ticks = 1;
    }
    ticks_per_slice = ticks;
    if (ticks_until_preempt > ticks_per_slice) {
        ticks_until_preempt = ticks_per_slice;
    }
}

void scheduler_ast_check(void)
{
    if (!scheduler_running) {
        return;
    }

    /* Preemption happens on timer IRQ; keep this as a no-op */
}
