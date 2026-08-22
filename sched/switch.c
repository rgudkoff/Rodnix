#include "internal.h"
#include "../trace/tracev2.h"
#include "../trace/bootlog.h"
#include "../kernel/arch/paging.h"
#include "../kernel/arch/x86_64/gdt.h"
#include "../kernel/core/cpu.h"
#include "../include/debug.h"
#include "../include/console.h"

static uint64_t scheduler_kernel_pml4 = 0;

static inline uint64_t scheduler_read_cr3(void)
{
    uint64_t cr3 = 0;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static void scheduler_switch_address_space(thread_t* next)
{
    if (!scheduler_kernel_pml4) {
        scheduler_kernel_pml4 = scheduler_read_cr3();
    }
    if (!next) {
        return;
    }

    uint64_t target_pml4 = scheduler_kernel_pml4;
    if (next->task && next->task->address_space) {
        target_pml4 = (uint64_t)(uintptr_t)next->task->address_space;
    }

    uint64_t current_pml4 = scheduler_read_cr3();
    if (target_pml4 && current_pml4 != target_pml4) {
        paging_switch_pml4(target_pml4);
    }
}

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
    thread_t* cur = thread_get_current();
    if (bootlog_is_verbose() && log_count < 8) {
        kprintf("[SCHED] irq switch: resched=%d current=%llu state=%d ready=%llu\n",
                resched_pending ? 1 : 0,
                (unsigned long long)(cur ? cur->thread_id : 0),
                (cur ? (int)cur->state : -1),
                (unsigned long long)stats.ready_tasks);
        log_count++;
    }
    TRACE_EVENT("sched: switch_from_irq");
    /* If no reschedule is pending and we already have a current thread, keep running */
    if (cur && !resched_pending) {
        in_scheduler = false;
        return frame;
    }
    resched_pending = false;

    /*
     * Hand back the thread this processor switched away from last time.
     *
     * It cannot be released at the moment of the switch. Until the stub
     * executes `mov rsp, rax`, this processor is still running on that
     * thread's kernel stack, and a thread in the run queue may be picked up
     * by another processor immediately -- which would have two CPUs on one
     * stack. Deferring to the next entry proves we are off it: we got here
     * on whatever stack we switched to.
     *
     * The cost is that a preempted thread becomes runnable one tick later
     * than it otherwise would.
     */
    {
        thread_t* pending = percpu_self()->sched_prev_pending;
        if (pending) {
            percpu_self()->sched_prev_pending = NULL;
            if (pending->state == THREAD_STATE_READY) {
                ready_enqueue(pending);
            }
        }
    }

    if (!cur) {
        thread_t* first = ready_dequeue();
        PANIC_IF(!first, "scheduler: no runnable threads on first switch");
        thread_set_current(first);
        if (first->task) {
            task_set_current(first->task);
        }
        scheduler_switch_address_space(first);
        scheduler_update_tss(first);
        sched_arch_apply_thread(first);
        stats.running_tasks = 1;
        stats.total_switches++;
        scheduler_thread_set_state(first, THREAD_STATE_RUNNING, "switch_first");
        scheduler_reset_timeslice(first);
        in_scheduler = false;
        return (interrupt_frame_t*)(uintptr_t)first->context.stack_pointer;
    }

    cur->context.stack_pointer = (uint64_t)(uintptr_t)frame;

    thread_t* next = ready_dequeue();
    if (next && cur->state == THREAD_STATE_RUNNING) {
        /* Only give the outgoing thread up once a replacement is in hand,
         * and even then only into this CPU's pending slot -- see above. */
        scheduler_thread_set_state(cur, THREAD_STATE_READY, "switch_preempt");
        percpu_self()->sched_prev_pending = cur;
    }
    if (!next || next == cur) {
        if (cur && cur->state != THREAD_STATE_DEAD) {
            scheduler_thread_set_state(cur, THREAD_STATE_RUNNING, "switch_continue_current");
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

    thread_t* prev = cur;
    thread_set_current(next);
    if (next->task) {
        task_set_current(next->task);
    }
    scheduler_switch_address_space(next);
    scheduler_update_tss(next);
    sched_arch_apply_thread(next);
    stats.running_tasks = 1;
    stats.total_switches++;

    if (prev && prev->state == THREAD_STATE_RUNNING) {
        scheduler_thread_set_state(prev, THREAD_STATE_READY, "switch_prev_ready");
        percpu_self()->sched_prev_pending = prev;
    }
    if (prev && prev->state == THREAD_STATE_DEAD) {
        scheduler_reap_enqueue(prev);
    }
    scheduler_thread_set_state(next, THREAD_STATE_RUNNING, "switch_next_running");
    scheduler_reset_timeslice(next);
    tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_SWITCH,
                 prev ? prev->thread_id : 0, next->thread_id);

    in_scheduler = false;
    if (bootlog_is_verbose() && log_count < 8) {
        kprintf("[SCHED] switch to tid=%llu\n",
                (unsigned long long)next->thread_id);
    }
    return (interrupt_frame_t*)(uintptr_t)next->context.stack_pointer;
}
