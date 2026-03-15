#include "internal.h"
#include "../../../include/debug.h"
#include "../../../include/error.h"

bool scheduler_initialized = false;
bool scheduler_running = false;
sched_policy_t current_policy = SCHED_POLICY_PRIORITY;
scheduler_stats_t stats = {0};

volatile bool in_scheduler = false;
uint32_t ticks_until_preempt = 1;
uint32_t ticks_per_slice = 1;
volatile bool resched_pending = false;
uint64_t sched_ticks = 0;

struct ready_queue_head ready_queues[READY_QUEUE_LEVELS];

scheduler_reap_stats_t reap_stats = {0};
waitq_t scheduler_sleep_waitq;

static bool scheduler_thread_transition_valid(thread_state_t from, thread_state_t to)
{
    if (from == to) {
        return true;
    }
    switch (from) {
    case THREAD_STATE_NEW:
        return to == THREAD_STATE_READY || to == THREAD_STATE_DEAD;
    case THREAD_STATE_READY:
        return to == THREAD_STATE_RUNNING || to == THREAD_STATE_BLOCKED || to == THREAD_STATE_DEAD;
    case THREAD_STATE_RUNNING:
        return to == THREAD_STATE_READY || to == THREAD_STATE_BLOCKED || to == THREAD_STATE_DEAD;
    case THREAD_STATE_BLOCKED:
        return to == THREAD_STATE_READY || to == THREAD_STATE_DEAD;
    case THREAD_STATE_SLEEPING:
        return to == THREAD_STATE_READY || to == THREAD_STATE_DEAD;
    case THREAD_STATE_DEAD:
        return false;
    default:
        return false;
    }
}

static bool scheduler_task_transition_valid(task_state_t from, task_state_t to)
{
    if (from == to) {
        return true;
    }
    switch (from) {
    case TASK_STATE_NEW:
        return to == TASK_STATE_READY || to == TASK_STATE_DEAD;
    case TASK_STATE_READY:
        return to == TASK_STATE_RUNNING || to == TASK_STATE_BLOCKED || to == TASK_STATE_ZOMBIE || to == TASK_STATE_DEAD;
    case TASK_STATE_RUNNING:
        return to == TASK_STATE_READY || to == TASK_STATE_BLOCKED || to == TASK_STATE_ZOMBIE || to == TASK_STATE_DEAD;
    case TASK_STATE_BLOCKED:
    case TASK_STATE_SLEEPING:
        return to == TASK_STATE_READY || to == TASK_STATE_ZOMBIE || to == TASK_STATE_DEAD;
    case TASK_STATE_ZOMBIE:
        return to == TASK_STATE_DEAD;
    case TASK_STATE_DEAD:
        return false;
    default:
        return false;
    }
}

void scheduler_thread_set_state(thread_t* thread, thread_state_t new_state, const char* reason)
{
    if (!thread) {
        return;
    }
    thread_state_t old_state = thread->state;
    if (!scheduler_thread_transition_valid(old_state, new_state)) {
        DEBUG_WARN("thread state transition tid=%llu %d->%d reason=%s",
                   (unsigned long long)thread->thread_id,
                   (int)old_state,
                   (int)new_state,
                   reason ? reason : "?");
    }
    thread->state = new_state;
}

void scheduler_task_set_state(task_t* task, task_state_t new_state, const char* reason)
{
    if (!task) {
        return;
    }
    task_state_t old_state = task->state;
    if (!scheduler_task_transition_valid(old_state, new_state)) {
        DEBUG_WARN("task state transition task=%llu %d->%d reason=%s",
                   (unsigned long long)task->task_id,
                   (int)old_state,
                   (int)new_state,
                   reason ? reason : "?");
    }
    task->state = new_state;
}

int scheduler_init(void)
{
    if (scheduler_initialized) {
        return 0;
    }

    thread_set_current(NULL);
    current_policy = SCHED_POLICY_PRIORITY;
    scheduler_running = false;
    for (int i = 0; i < READY_QUEUE_LEVELS; i++) {
        TAILQ_INIT(&ready_queues[i]);
    }
    waitq_init(&scheduler_sleep_waitq, "scheduler_sleep");
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
    thread_t* cur = thread_get_current();
    if (!cur) {
        return NULL;
    }
    return cur->task;
}

int scheduler_add_thread(thread_t* thread)
{
    if (!thread) {
        return -1;
    }
    if (thread->state != THREAD_STATE_NEW && thread->state != THREAD_STATE_READY) {
        DEBUG_WARN("add_thread: thread %llu state=%d", (unsigned long long)thread->thread_id, thread->state);
    }
    scheduler_thread_set_state(thread, THREAD_STATE_READY, "scheduler_add_thread");
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

int scheduler_get_waitq_stats(scheduler_waitq_stats_t* out_stats)
{
    if (!out_stats) {
        return RDNX_E_INVALID;
    }
    out_stats->sleep_waiters = waitq_count(&scheduler_sleep_waitq);
    out_stats->timed_waiters = waitq_timed_count();
    return RDNX_OK;
}

uint64_t scheduler_get_ticks(void)
{
    return sched_ticks;
}
