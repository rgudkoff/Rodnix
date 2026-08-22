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

/*
 * Pick what to run next: whatever the queue offers, or this processor's idle
 * thread when it offers nothing.
 *
 * The idle thread is why the switch path never has to answer "nothing to
 * run". It is per-CPU and never enters the shared queue, so it is always
 * available and never contended.
 */
static thread_t* sched_take_next(void)
{
    thread_t* next = ready_dequeue();
    if (next) {
        return next;
    }
    return percpu_self()->sched_idle;
}

/*
 * Take ownership of a thread's stack.
 *
 * The wait is the whole point of on_cpu. A thread reaches the run queue
 * while the processor that preempted it is still executing on its kernel
 * stack -- everything between the enqueue and the stub's stack switch. This
 * spins over exactly that window, which is a handful of instructions.
 */
static void sched_claim(thread_t* t)
{
    if (!t) {
        return;
    }
    while (__atomic_load_n(&t->on_cpu, __ATOMIC_ACQUIRE) != 0) {
        __asm__ volatile ("pause");
    }
    __atomic_store_n(&t->on_cpu, 1u, __ATOMIC_RELEASE);
}

interrupt_frame_t* scheduler_switch_from_irq(interrupt_frame_t* frame)
{
    if (!scheduler_running || !frame) {
        return frame;
    }
    if (in_scheduler) {
        return frame;
    }

    /* Someone on this processor is holding a spinlock. Switching now would
     * leave the lock held by a thread that is no longer running, and the
     * next thread to want it would spin until the holder is scheduled again
     * -- which, if it is spinning too, is never. The reschedule is not lost,
     * only deferred: resched_pending stays set and the next tick takes it. */
    if (percpu_preempt_blocked()) {
        resched_pending = true;
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


    if (!cur) {
        thread_t* first = sched_take_next();
        PANIC_IF(!first, "scheduler: no runnable threads on first switch");
        sched_claim(first);
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

    thread_t* next = sched_take_next();
    if (next && next != cur && cur->state == THREAD_STATE_RUNNING &&
        cur != percpu_self()->sched_idle) {
        /* Released to the queue straight away. That is safe now only because
         * of the handshake: whoever takes it will wait on cur->on_cpu, which
         * this processor's stub clears once it is off cur's stack. */
        scheduler_thread_set_state(cur, THREAD_STATE_READY, "switch_preempt");
        ready_enqueue(cur);
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

    sched_claim(next);

    thread_t* prev = cur;
    /* Tell the stub whose stack it is leaving. Cleared there, after the
     * switch, which is the only moment the claim can honestly be dropped. */
    percpu_self()->sched_prev_oncpu = &prev->on_cpu;

    thread_set_current(next);
    if (next->task) {
        task_set_current(next->task);
    }
    scheduler_switch_address_space(next);
    scheduler_update_tss(next);
    sched_arch_apply_thread(next);
    stats.running_tasks = 1;
    stats.total_switches++;

    if (prev && prev->state == THREAD_STATE_RUNNING &&
        prev != percpu_self()->sched_idle) {
        scheduler_thread_set_state(prev, THREAD_STATE_READY, "switch_prev_ready");
        ready_enqueue(prev);
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
