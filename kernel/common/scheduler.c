/**
 * @file scheduler.c
 * @brief Task scheduler implementation
 */

#include "scheduler.h"
#include "../core/interrupts.h"
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
static volatile bool resched_pending = false;

static void ready_enqueue(thread_t* thread);
static thread_t* ready_dequeue(void);
static void scheduler_switch_to(thread_t* next);

static void scheduler_yield_internal(bool irq_context)
{
    if (!scheduler_running) {
        return;
    }

    if (in_scheduler) {
        return;
    }

    in_scheduler = true;
    if (!irq_context) {
        interrupts_disable();
    }

    if (!ready_head) {
        if (!irq_context) {
            interrupts_enable();
        }
        in_scheduler = false;
        return;
    }

    if (current_thread && current_thread->state == THREAD_STATE_RUNNING) {
        current_thread->state = THREAD_STATE_READY;
        ready_enqueue(current_thread);
    }

    thread_t* next = ready_dequeue();
    if (next) {
        scheduler_switch_to(next);
    }

    if (!irq_context) {
        interrupts_enable();
    }
    in_scheduler = false;
}

static void ready_enqueue(thread_t* thread)
{
    if (!thread) {
        return;
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
    ticks_until_preempt = 1;
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
    
    thread->state = THREAD_STATE_READY;
    ready_enqueue(thread);
    
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
    interrupts_disable();

    current_thread->state = THREAD_STATE_BLOCKED;
    stats.blocked_tasks++;

    thread_t* next = ready_dequeue();
    if (next) {
        scheduler_switch_to(next);
    } else {
        current_thread->state = THREAD_STATE_RUNNING;
        if (stats.blocked_tasks > 0) {
            stats.blocked_tasks--;
        }
    }

    interrupts_enable();
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
        ticks_until_preempt = 1;
        resched_pending = true;
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
