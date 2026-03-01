#include "internal.h"
#include "../../../include/debug.h"
#include "../../../include/error.h"

bool scheduler_initialized = false;
bool scheduler_running = false;
thread_t* current_thread = NULL;
sched_policy_t current_policy = SCHED_POLICY_PRIORITY;
scheduler_stats_t stats = {0};

volatile bool in_scheduler = false;
uint32_t ticks_until_preempt = 1;
uint32_t ticks_per_slice = 1;
volatile bool resched_pending = false;
uint64_t sched_ticks = 0;

thread_t* ready_head[READY_QUEUE_LEVELS] = {0};
thread_t* ready_tail[READY_QUEUE_LEVELS] = {0};

scheduler_reap_stats_t reap_stats = {0};

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

    scheduler_reaper_start();

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

int scheduler_set_policy(sched_policy_t policy)
{
    if (policy > SCHED_POLICY_CFS) {
        return -1;
    }

    current_policy = policy;

    /* TODO: Reorganize queues based on new policy */

    return 0;
}

int scheduler_get_stats(scheduler_stats_t* out_stats)
{
    if (!out_stats) {
        return RDNX_E_INVALID;
    }

    *out_stats = stats;
    return RDNX_OK;
}

int scheduler_get_reap_stats(scheduler_reap_stats_t* out_stats)
{
    if (!out_stats) {
        return RDNX_E_INVALID;
    }
    reap_stats.queue_len = scheduler_reap_queue_len();
    *out_stats = reap_stats;
    return RDNX_OK;
}

uint64_t scheduler_get_ticks(void)
{
    return sched_ticks;
}
