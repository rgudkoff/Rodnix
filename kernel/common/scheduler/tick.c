#include "internal.h"

void scheduler_tick(void)
{
    if (!scheduler_running) {
        return;
    }

    sched_ticks++;
    waitq_tick(sched_ticks);
    thread_t* cur = thread_get_current();
    if (cur && cur->state == THREAD_STATE_RUNNING) {
        cur->sched_usage = (cur->sched_usage * 7) / 8;
        cur->sched_usage++;
        /* Обновить CPU-счётчики группы (task_t.thread_group) */
        if (cur->task) {
            cur->task->thread_group.cpu_ticks++;
            cur->task->thread_group.last_run_tick = sched_ticks;
        }
        if (cur->sched_class == SCHED_CLASS_TIMESHARE) {
            if ((cur->sched_usage % PENALTY_STEP_TICKS) == 0) {
                int base = cur->base_priority;
                int dyn = cur->dyn_priority - 1;
                cur->dyn_priority = clamp_dyn_priority(dyn, base);
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
