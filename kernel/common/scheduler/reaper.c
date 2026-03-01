#include "internal.h"
#include "../heap.h"
#include "../tracev2.h"
#include "../../../include/error.h"
#include "../../../include/debug.h"

#define REAP_QUEUE_SIZE 64
#define REAP_GRACE_TICKS 128

static thread_t* reap_queue[REAP_QUEUE_SIZE] = {0};
static uint32_t reap_head = 0;
static uint32_t reap_tail = 0;
static task_t* reaper_task = NULL;
static thread_t* reaper_thread = NULL;

static thread_t* scheduler_reap_dequeue(void)
{
    if (reap_head == reap_tail) {
        return NULL;
    }
    thread_t* t = reap_queue[reap_head];
    reap_queue[reap_head] = NULL;
    reap_head = (reap_head + 1u) % REAP_QUEUE_SIZE;
    return t;
}

static thread_t* scheduler_reap_peek(void)
{
    if (reap_head == reap_tail) {
        return NULL;
    }
    return reap_queue[reap_head];
}

static void scheduler_reaper_main(void* arg)
{
    (void)arg;
    for (;;) {
        scheduler_reap_dead_threads();
        reap_stats.queue_len = scheduler_reap_queue_len();
        scheduler_yield();
    }
}

uint32_t scheduler_reap_queue_len(void)
{
    return (reap_tail + REAP_QUEUE_SIZE - reap_head) % REAP_QUEUE_SIZE;
}

void scheduler_reap_enqueue(thread_t* dead_thread)
{
    if (!dead_thread || dead_thread->reap_queued) {
        return;
    }
    uint32_t next_tail = (reap_tail + 1u) % REAP_QUEUE_SIZE;
    if (next_tail == reap_head) {
        /*
         * Losing DEAD threads leaks stacks/descriptors and breaks lifecycle
         * accounting. Fail fast instead of silently dropping cleanup work.
         */
        reap_stats.dropped++;
        tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_REAPER_OVERFLOW,
                     reap_stats.queue_len, reap_stats.dropped);
        PANIC("scheduler reaper queue overflow");
    }
    dead_thread->reap_after_tick = sched_ticks + REAP_GRACE_TICKS;
    dead_thread->reap_queued = 1;
    reap_queue[reap_tail] = dead_thread;
    reap_tail = next_tail;
    reap_stats.enqueued++;
    uint32_t qlen = scheduler_reap_queue_len();
    if (qlen > reap_stats.queue_hwm) {
        reap_stats.queue_hwm = qlen;
    }
}

void scheduler_reap_dead_threads(void)
{
    reap_stats.runs++;
    for (;;) {
        thread_t* head = scheduler_reap_peek();
        if (!head) {
            break;
        }
        if (head->reap_after_tick > sched_ticks) {
            reap_stats.deferred++;
            break;
        }
        thread_t* dead = scheduler_reap_dequeue();
        if (!dead) {
            break;
        }
        dead->reap_queued = 0;
        task_t* owner = dead->task;
        if (owner && owner->thread_count > 0) {
            owner->thread_count--;
        }
        if (dead->stack) {
            task_kernel_stack_retire(dead->stack, dead->stack_size);
            dead->stack = NULL;
            dead->stack_size = 0;
        }
        dead->task = NULL;
        kfree(dead);
        reap_stats.reaped++;
        if (owner && owner != task_get_current()) {
            if (owner->thread_count == 0) {
                scheduler_task_set_state(owner, TASK_STATE_DEAD, "reaper_last_thread");
                task_destroy(owner);
            } else {
                scheduler_task_set_state(owner, TASK_STATE_ZOMBIE, "reaper_threads_remaining");
            }
        }
    }
}

void scheduler_reap_finished(void)
{
    scheduler_reap_dead_threads();
    reap_stats.queue_len = scheduler_reap_queue_len();
}

void scheduler_reaper_start(void)
{
    if (reaper_thread) {
        return;
    }
    reaper_task = task_create();
    if (!reaper_task) {
        return;
    }
    scheduler_task_set_state(reaper_task, TASK_STATE_READY, "reaper_start");
    reaper_thread = thread_create(reaper_task, scheduler_reaper_main, NULL);
    if (!reaper_thread) {
        task_destroy(reaper_task);
        reaper_task = NULL;
        return;
    }
    reaper_thread->priority = 16;
    scheduler_add_thread(reaper_thread);
}
