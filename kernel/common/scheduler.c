/**
 * @file scheduler.c
 * @brief Task scheduler implementation
 */

#include "scheduler.h"
#include "../core/interrupts.h"
#include "../../include/debug.h"
#include <stddef.h>
#include <stdbool.h>

/* Scheduler state */
static bool scheduler_initialized = false;
static bool scheduler_running = false;
static thread_t* current_thread = NULL;
static sched_policy_t current_policy = SCHED_POLICY_RR;
static scheduler_stats_t stats = {0};

/* Ready queue (simple FIFO) */
static thread_t* ready_head = NULL;
static thread_t* ready_tail = NULL;

static volatile bool in_scheduler = false;
static uint32_t ticks_until_preempt = 1;
static uint32_t ticks_per_slice = 1;
static volatile bool resched_pending = false;

static void ready_enqueue(thread_t* thread);
static thread_t* ready_dequeue(void);
static void scheduler_switch_to(thread_t* next);

static void scheduler_yield_internal(bool irq_context)
{
    (void)irq_context;
    if (!scheduler_running) {
        return;
    }

    if (in_scheduler) {
        return;
    }

    in_scheduler = true;

    if (!ready_head) {
        in_scheduler = false;
        return;
    }

    if (current_thread && current_thread->state == THREAD_STATE_RUNNING) {
        current_thread->state = THREAD_STATE_READY;
        ready_enqueue(current_thread);
    }

    thread_t* next = ready_dequeue();
    if (!next || next == current_thread) {
        if (current_thread) {
            current_thread->state = THREAD_STATE_RUNNING;
        }
        in_scheduler = false;
        return;
    }

    /* Clear guard before switching so the next thread can yield. */
    in_scheduler = false;
    scheduler_switch_to(next);
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
    if (!ready_tail) {
        ready_head = thread;
        ready_tail = thread;
    } else {
        ready_tail->sched_next = thread;
        ready_tail = thread;
    }
    stats.ready_tasks++;
}

static thread_t* ready_dequeue(void)
{
    thread_t* thread = ready_head;
    if (!thread) {
        return NULL;
    }

    if (thread->state != THREAD_STATE_READY) {
        DEBUG_WARN("ready_dequeue: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
    }
    ready_head = thread->sched_next;
    if (!ready_head) {
        ready_tail = NULL;
    }
    thread->sched_next = NULL;
    if (stats.ready_tasks > 0) {
        stats.ready_tasks--;
    }
    return thread;
}

static void scheduler_switch_to(thread_t* next)
{
    if (!next || next == current_thread) {
        return;
    }

    if (current_thread &&
        current_thread->state != THREAD_STATE_RUNNING &&
        current_thread->state != THREAD_STATE_READY) {
        DEBUG_WARN("switch_to: current thread %llu state=%d",
                   (unsigned long long)current_thread->thread_id, current_thread->state);
    }
    if (next->state != THREAD_STATE_READY) {
        DEBUG_WARN("switch_to: next thread %llu state=%d", (unsigned long long)next->thread_id, next->state);
    }
    thread_t* prev = current_thread;
    current_thread = next;
    thread_set_current(next);
    stats.running_tasks = 1;
    stats.total_switches++;

    if (prev) {
        prev->state = THREAD_STATE_READY;
    }
    next->state = THREAD_STATE_RUNNING;

    thread_switch(prev, next);
}

int scheduler_init(void)
{
    if (scheduler_initialized) {
        return 0;
    }
    
    current_thread = NULL;
    thread_set_current(NULL);
    current_policy = SCHED_POLICY_RR;
    scheduler_running = false;
    ready_head = NULL;
    ready_tail = NULL;
    ticks_per_slice = 1;
    ticks_until_preempt = ticks_per_slice;
    resched_pending = false;
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
    
    if (!current_thread) {
        thread_t* next = ready_dequeue();
        if (next) {
            scheduler_switch_to(next);
        }
    }
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
    stats.blocked_tasks++;

    thread_t* next = ready_dequeue();
    if (next) {
        /* Clear guard before switching so the next thread can yield. */
        in_scheduler = false;
        scheduler_switch_to(next);
        return;
    }

    current_thread->state = THREAD_STATE_RUNNING;
    if (stats.blocked_tasks > 0) {
        stats.blocked_tasks--;
    }
    in_scheduler = false;
}

void scheduler_unblock(thread_t* thread)
{
    if (!thread) {
        return;
    }
    
    if (thread->state == THREAD_STATE_BLOCKED) {
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

    if (resched_pending) {
        resched_pending = false;
        scheduler_yield_internal(false);
    }
}

int scheduler_get_stats(scheduler_stats_t* out_stats)
{
    if (!out_stats) {
        return -1;
    }
    
    *out_stats = stats;
    return 0;
}
