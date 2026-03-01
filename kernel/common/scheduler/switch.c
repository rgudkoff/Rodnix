#include "internal.h"
#include "../tracev2.h"
#include "../../../include/debug.h"

interrupt_frame_t* scheduler_switch_from_irq(interrupt_frame_t* frame)
{
    if (!scheduler_running || !frame) {
        return frame;
    }
    if (in_scheduler) {
        return frame;
    }

    in_scheduler = true;
    static int log_count = 0;
    if (log_count < 8) {
        kprintf("[SCHED] irq switch: resched=%d current=%llu state=%d ready=%llu\n",
                resched_pending ? 1 : 0,
                (unsigned long long)(current_thread ? current_thread->thread_id : 0),
                (current_thread ? (int)current_thread->state : -1),
                (unsigned long long)stats.ready_tasks);
        log_count++;
    }
    TRACE_EVENT("sched: switch_from_irq");
    /* If no reschedule is pending and we already have a current thread, keep running */
    if (current_thread && !resched_pending) {
        in_scheduler = false;
        return frame;
    }
    resched_pending = false;

    if (!current_thread) {
        thread_t* first = ready_dequeue();
        PANIC_IF(!first, "scheduler: no runnable threads on first switch");
        current_thread = first;
        thread_set_current(first);
        if (first->task) {
            task_set_current(first->task);
        }
        scheduler_update_tss(first);
        stats.running_tasks = 1;
        stats.total_switches++;
        scheduler_thread_set_state(first, THREAD_STATE_RUNNING, "switch_first");
        scheduler_reset_timeslice(first);
        in_scheduler = false;
        return (interrupt_frame_t*)(uintptr_t)first->context.stack_pointer;
    }

    current_thread->context.stack_pointer = (uint64_t)(uintptr_t)frame;
    if (current_thread->state == THREAD_STATE_RUNNING) {
        scheduler_thread_set_state(current_thread, THREAD_STATE_READY, "switch_preempt");
        ready_enqueue(current_thread);
    }

    thread_t* next = ready_dequeue();
    if (!next || next == current_thread) {
        if (current_thread && current_thread->state != THREAD_STATE_DEAD) {
            scheduler_thread_set_state(current_thread, THREAD_STATE_RUNNING, "switch_continue_current");
            in_scheduler = false;
            return frame;
        }
        /*
         * Never resume a DEAD thread.
         * This indicates that no runnable fallback thread exists.
         */
        PANIC_IF(true, "scheduler: no runnable threads after current thread exit");
        in_scheduler = false;
        return frame;
    }

    thread_t* prev = current_thread;
    current_thread = next;
    thread_set_current(next);
    if (next->task) {
        task_set_current(next->task);
    }
    scheduler_update_tss(next);
    stats.running_tasks = 1;
    stats.total_switches++;

    if (prev && prev->state == THREAD_STATE_RUNNING) {
        scheduler_thread_set_state(prev, THREAD_STATE_READY, "switch_prev_ready");
    }
    if (prev && prev->state == THREAD_STATE_DEAD) {
        scheduler_reap_enqueue(prev);
    }
    scheduler_thread_set_state(next, THREAD_STATE_RUNNING, "switch_next_running");
    scheduler_reset_timeslice(next);
    tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_SWITCH,
                 prev ? prev->thread_id : 0, next->thread_id);

    in_scheduler = false;
    if (log_count < 8) {
        kprintf("[SCHED] switch to tid=%llu\n",
                (unsigned long long)next->thread_id);
    }
    return (interrupt_frame_t*)(uintptr_t)next->context.stack_pointer;
}
