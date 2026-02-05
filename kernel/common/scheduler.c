/**
 * @file scheduler.c
 * @brief Task scheduler implementation
 */

#include "scheduler.h"
#include "../core/interrupts.h"
#include "../arch/x86_64/interrupt_frame.h"
#include "../../include/debug.h"
#include "../../include/error.h"
#include <stddef.h>
#include <stdbool.h>

/* Scheduler state */
static bool scheduler_initialized = false;
static bool scheduler_running = false;
static thread_t* current_thread = NULL;
static sched_policy_t current_policy = SCHED_POLICY_PRIORITY;
static scheduler_stats_t stats = {0};

/* Ready queues (MLQ with coarse priority bands) */
#define READY_QUEUE_LEVELS 3
static thread_t* ready_head[READY_QUEUE_LEVELS] = {0};
static thread_t* ready_tail[READY_QUEUE_LEVELS] = {0};

/* Dynamic priority tuning (simple, v0) */
#define BOOST_THRESHOLD_TICKS 5
#define BOOST_MAX 32
#define PENALTY_MAX 32
#define PENALTY_STEP_TICKS 4

/* Time quantum tuning (ticks) */
#define REALTIME_QUANTUM_TICKS 1
#define TIMESHARE_PRIO_STEP    64
#define TIMESHARE_MAX_BONUS    3
#define CPU_BOUND_THRESHOLD    8
#define CPU_BOUND_EXTRA_TICKS  2

static volatile bool in_scheduler = false;
static uint32_t ticks_until_preempt = 1;
static uint32_t ticks_per_slice = 1;
static volatile bool resched_pending = false;
static uint64_t sched_ticks = 0;

static void ready_enqueue(thread_t* thread);
static thread_t* ready_dequeue(void);
static int ready_queue_index_for_thread(const thread_t* thread);
static void scheduler_reset_timeslice(const thread_t* thread);

static int clamp_dyn_priority(int value, int base)
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

static int thread_effective_priority(const thread_t* thread)
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

static void scheduler_reset_timeslice(const thread_t* thread)
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

static void scheduler_yield_internal(bool irq_context)
{
    (void)irq_context;
    if (!scheduler_running) {
        return;
    }
    /* Request preemption on next timer interrupt */
    resched_pending = true;
}

static void ready_enqueue(thread_t* thread)
{
    if (!thread) {
        return;
    }

    if (thread->sched_next) {
        DEBUG_WARN("ready_enqueue: thread %llu already queued", (unsigned long long)thread->thread_id);
    }
    thread->sched_next = NULL;
    int q = ready_queue_index_for_thread(thread);
    if (q < 0 || q >= READY_QUEUE_LEVELS) {
        q = READY_QUEUE_LEVELS - 1;
    }
    if (!ready_tail[q]) {
        ready_head[q] = thread;
        ready_tail[q] = thread;
    } else {
        ready_tail[q]->sched_next = thread;
        ready_tail[q] = thread;
    }
    stats.ready_tasks++;
}

static thread_t* ready_dequeue(void)
{
    int start = READY_QUEUE_LEVELS - 1;
    int end = 0;

    if (current_policy == SCHED_POLICY_RR || current_policy == SCHED_POLICY_FIFO) {
        start = 1;
        end = 1;
    }

    if (start == end) {
        int q = start;
        thread_t* thread = ready_head[q];
        if (!thread) {
            return NULL;
        }

        if (thread->state != THREAD_STATE_READY) {
            DEBUG_WARN("ready_dequeue: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
        }
        ready_head[q] = thread->sched_next;
        if (!ready_head[q]) {
            ready_tail[q] = NULL;
        }
        thread->sched_next = NULL;
        if (stats.ready_tasks > 0) {
            stats.ready_tasks--;
        }
        return thread;
    }

    for (int q = start; q >= end; q--) {
        thread_t* thread = ready_head[q];
        if (!thread) {
            continue;
        }

        if (thread->state != THREAD_STATE_READY) {
            DEBUG_WARN("ready_dequeue: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
        }
        ready_head[q] = thread->sched_next;
        if (!ready_head[q]) {
            ready_tail[q] = NULL;
        }
        thread->sched_next = NULL;
        if (stats.ready_tasks > 0) {
            stats.ready_tasks--;
        }
        return thread;
    }

    return NULL;
}

static int ready_queue_index_for_thread(const thread_t* thread)
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

int scheduler_init(void)
{
    if (scheduler_initialized) {
        return 0;
    }
    
    current_thread = NULL;
    thread_set_current(NULL);
    current_policy = SCHED_POLICY_PRIORITY;
    scheduler_running = false;
    for (int i = 0; i < READY_QUEUE_LEVELS; i++) {
        ready_head[i] = NULL;
        ready_tail[i] = NULL;
    }
    ticks_per_slice = 1;
    ticks_until_preempt = ticks_per_slice;
    resched_pending = false;
    sched_ticks = 0;
    stats.running_tasks = 0;
    stats.ready_tasks = 0;
    stats.blocked_tasks = 0;
    
    scheduler_initialized = true;
    return 0;
}

void scheduler_start(void)
{
    if (!scheduler_initialized) {
        scheduler_init();
    }
    
    scheduler_running = true;
    ticks_until_preempt = ticks_per_slice;

    /* Kick preemption to start the first thread on the next timer IRQ */
    resched_pending = true;
    /* Force a timer-like IRQ to start the first thread */
    __asm__ volatile ("int $32");
    /* If we return here, we did not switch yet */
}

int scheduler_add_task(task_t* task)
{
    if (!task) {
        return -1;
    }
    
    /* TODO: Add task to scheduler */
    
    stats.total_tasks++;
    return 0;
}

int scheduler_remove_task(task_t* task)
{
    if (!task) {
        return -1;
    }
    
    /* TODO: Remove task from scheduler */
    
    return 0;
}

task_t* scheduler_get_current_task(void)
{
    if (!current_thread) {
        return NULL;
    }
    
    return current_thread->task;
}

int scheduler_add_thread(thread_t* thread)
{
    if (!thread) {
        return -1;
    }
    if (thread->state != THREAD_STATE_NEW && thread->state != THREAD_STATE_READY) {
        DEBUG_WARN("add_thread: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
    }
    thread->state = THREAD_STATE_READY;
    thread->base_priority = thread->priority;
    thread->dyn_priority = thread->priority;
    thread->inherited_priority = thread->priority;
    thread->has_inherited = 0;
    ready_enqueue(thread);
    stats.total_tasks++;
    
    return 0;
}

int scheduler_remove_thread(thread_t* thread)
{
    if (!thread) {
        return -1;
    }
    
    /* TODO: Remove thread from queues */
    
    return 0;
}

thread_t* scheduler_get_current_thread(void)
{
    return thread_get_current();
}

void scheduler_yield(void)
{
    scheduler_yield_internal(false);
}

void scheduler_block(void)
{
    if (!current_thread) {
        return;
    }
    
    if (in_scheduler) {
        return;
    }

    in_scheduler = true;

    if (current_thread->state != THREAD_STATE_RUNNING) {
        DEBUG_WARN("block: current thread %llu state=%d", (unsigned long long)current_thread->thread_id, current_thread->state);
    }
    current_thread->state = THREAD_STATE_BLOCKED;
    current_thread->last_sleep_tick = sched_ticks;
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
        thread->state = THREAD_STATE_READY;
        if (stats.blocked_tasks > 0) {
            stats.blocked_tasks--;
        }
        ready_enqueue(thread);
    } else {
        DEBUG_WARN("unblock: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
    }
}

void scheduler_sleep(uint64_t milliseconds)
{
    if (!current_thread) {
        return;
    }
    
    (void)milliseconds;
    scheduler_block();
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

int scheduler_set_policy(sched_policy_t policy)
{
    if (policy > SCHED_POLICY_CFS) {
        return -1;
    }
    
    current_policy = policy;
    
    /* TODO: Reorganize queues based on new policy */
    
    return 0;
}

void scheduler_tick(void)
{
    if (!scheduler_running) {
        return;
    }

    sched_ticks++;
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

int scheduler_get_stats(scheduler_stats_t* out_stats)
{
    if (!out_stats) {
        return RDNX_E_INVALID;
    }
    
    *out_stats = stats;
    return RDNX_OK;
}

uint64_t scheduler_get_ticks(void)
{
    return sched_ticks;
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
        stats.running_tasks = 1;
        stats.total_switches++;
        first->state = THREAD_STATE_RUNNING;
        scheduler_reset_timeslice(first);
        in_scheduler = false;
        return (interrupt_frame_t*)(uintptr_t)first->context.stack_pointer;
    }

    current_thread->context.stack_pointer = (uint64_t)(uintptr_t)frame;
    if (current_thread->state == THREAD_STATE_RUNNING) {
        current_thread->state = THREAD_STATE_READY;
        ready_enqueue(current_thread);
    }

    thread_t* next = ready_dequeue();
    if (!next || next == current_thread) {
        if (current_thread) {
            current_thread->state = THREAD_STATE_RUNNING;
        }
        in_scheduler = false;
        return frame;
    }

    thread_t* prev = current_thread;
    current_thread = next;
    thread_set_current(next);
    if (next->task) {
        task_set_current(next->task);
    }
    stats.running_tasks = 1;
    stats.total_switches++;

    if (prev) {
        prev->state = THREAD_STATE_READY;
    }
    next->state = THREAD_STATE_RUNNING;
    scheduler_reset_timeslice(next);

    in_scheduler = false;
    return (interrupt_frame_t*)(uintptr_t)next->context.stack_pointer;
}

void scheduler_inherit_priority(thread_t* target, const thread_t* donor)
{
    if (!target || !donor) {
        return;
    }
    int donor_prio = thread_effective_priority(donor);
    if (target->inherit_depth < 4) {
        target->inherit_stack[target->inherit_depth++] = target->inherited_priority;
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
    if (target->inherit_depth > 0) {
        target->inherited_priority = target->inherit_stack[--target->inherit_depth];
    } else {
        target->inherited_priority = target->dyn_priority;
    }
    target->has_inherited = 0;
}

void scheduler_debug_dump(void)
{
    kputs("[SCHED] ---- dump ----\n");
    kprintf("[SCHED] ticks=%llu running=%d ready=%llu blocked=%llu\n",
            (unsigned long long)sched_ticks,
            scheduler_running ? 1 : 0,
            (unsigned long long)stats.ready_tasks,
            (unsigned long long)stats.blocked_tasks);
    if (current_thread) {
        kprintf("[SCHED] current tid=%llu prio=%u dyn=%d class=%u state=%d\n",
                (unsigned long long)current_thread->thread_id,
                (unsigned)current_thread->priority,
                (int)current_thread->dyn_priority,
                (unsigned)current_thread->sched_class,
                (int)current_thread->state);
    } else {
        kputs("[SCHED] current none\n");
    }

    for (int q = READY_QUEUE_LEVELS - 1; q >= 0; q--) {
        uint32_t count = 0;
        thread_t* it = ready_head[q];
        while (it && count < 1024) {
            count++;
            it = it->sched_next;
        }
        kprintf("[SCHED] q%d count=%u\n", q, count);
    }
    kputs("[SCHED] --------------\n");
}
