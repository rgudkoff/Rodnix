#include "internal.h"
#include "../kernel/fabric/spin.h"
#include "../lib/heap.h"
#include "../trace/tracev2.h"
#include "../kernel/unix/unix_layer.h"
#include "../mm/vm_map.h"
#include "../kernel/core/interrupts.h"
#include "../include/error.h"
#include "../include/debug.h"
#include "../include/console.h"

#define REAP_QUEUE_SIZE 64
#define REAP_GRACE_TICKS 128

static thread_t* reap_queue[REAP_QUEUE_SIZE] = {0};
static uint32_t reap_head = 0;
static uint32_t reap_tail = 0;
static task_t* reaper_task = NULL;
static thread_t* reaper_thread = NULL;

/* Guards the reap list against concurrent producers on other CPUs. */
static spinlock_t reaper_spin;

static inline uint64_t reaper_lock(void)
{
    return spinlock_lock_irqsave(&reaper_spin);
}

static inline void reaper_unlock(uint64_t flags)
{
    spinlock_unlock_irqrestore(&reaper_spin, flags);
}

/*
 * Забрать голову очереди — только если её уже можно освобождать.
 *
 * Проверки готовности выполняются под замком и до извлечения. Прежний код
 * сначала вынимал поток, потом смотрел на on_cpu и, если процессор ещё был
 * на его стеке, просто бросал указатель: обратно в очередь поток не
 * возвращался, а reap_queued оставался поднятым и запрещал повторную
 * постановку. Стек и дескрипторы такого потока текли навсегда, ожидающие
 * не уведомлялись. Неготовая голова теперь остаётся в очереди до
 * следующего круга — жнецу ждать нечего, поток никуда не денется.
 *
 * Почему on_cpu вообще проверяется: поток попадает сюда в момент пометки
 * DEAD, раньше, чем его процессор сошёл с его стека (стаб чистит on_cpu
 * после смены rsp). Освободить стек в это окно — значит отравить память,
 * по которой процессор ещё исполняется; найдено по #GP с
 * rip = 0xcccccccccccccccc.
 */
static thread_t* scheduler_reap_take_ready(void)
{
    uint64_t old = reaper_lock();
    if (reap_head == reap_tail) {
        reaper_unlock(old);
        return NULL;
    }
    thread_t* t = reap_queue[reap_head];
    if (t->reap_after_tick > sched_ticks ||
        __atomic_load_n(&t->on_cpu, __ATOMIC_ACQUIRE) != 0) {
        reap_stats.deferred++;
        reaper_unlock(old);
        return NULL;
    }
    reap_queue[reap_head] = NULL;
    reap_head = (reap_head + 1u) % REAP_QUEUE_SIZE;
    reaper_unlock(old);
    return t;
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

/* Caller must hold the reaper lock. Split out because the enqueue path needs
 * the length while already holding it, and a real spinlock -- unlike the
 * cli-based lock this replaced -- deadlocks rather than tolerating the
 * nested acquire. */
static inline uint32_t reap_queue_len_locked(void)
{
    return (reap_tail + REAP_QUEUE_SIZE - reap_head) % REAP_QUEUE_SIZE;
}

uint32_t scheduler_reap_queue_len(void)
{
    uint64_t old = reaper_lock();
    uint32_t qlen = reap_queue_len_locked();
    reaper_unlock(old);
    return qlen;
}

void scheduler_reap_enqueue(thread_t* dead_thread)
{
    if (!dead_thread || dead_thread->reap_queued) {
        return;
    }
    uint64_t old = reaper_lock();
    uint32_t next_tail = (reap_tail + 1u) % REAP_QUEUE_SIZE;
    if (next_tail == reap_head) {
        /*
         * Losing DEAD threads leaks stacks/descriptors and breaks lifecycle
         * accounting. Fail fast instead of silently dropping cleanup work.
         */
        reap_stats.dropped++;
        tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_REAPER_OVERFLOW,
                     reap_stats.queue_len, reap_stats.dropped);
        reaper_unlock(old);
        PANIC("scheduler reaper queue overflow");
    }
    dead_thread->reap_after_tick = sched_ticks + REAP_GRACE_TICKS;
    dead_thread->reap_queued = 1;
    reap_queue[reap_tail] = dead_thread;
    reap_tail = next_tail;
    reap_stats.enqueued++;
    uint32_t qlen = reap_queue_len_locked();
    if (qlen > reap_stats.queue_hwm) {
        reap_stats.queue_hwm = qlen;
    }
    reaper_unlock(old);
}

void scheduler_reap_dead_threads(void)
{
    reap_stats.runs++;
    for (;;) {
        thread_t* dead = scheduler_reap_take_ready();
        if (!dead) {
            break;
        }
        dead->reap_queued = 0;
        task_t* owner = dead->task;
        if (owner) {
            TAILQ_REMOVE(&owner->threads, dead, task_link);
        }
        if (owner && owner->thread_count > 0) {
            owner->thread_count--;
        }
        if (owner && owner->main_thread == dead) {
            owner->main_thread = NULL;
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
                /*
                 * Lifecycle ownership rule:
                 * - Reaper always reclaims dead threads.
                 * - User task object is reclaimed by waitpid() only.
                 * - Kernel/orphan tasks (parent_task_id == 0) are reclaimed here.
                 *
                 * This avoids waitpid/reaper double-destroy races on task_t.
                 */
                if (owner->parent_task_id == 0) {
                    scheduler_task_set_state(owner, TASK_STATE_DEAD, "reaper_last_thread");
                    task_destroy(owner);
                } else {
                    const proc_t* owner_proc = task_proc(owner);
                    if (owner_proc && owner_proc->waited) {
                        scheduler_task_set_state(owner, TASK_STATE_DEAD, "reaper_waited_destroy");
                        task_destroy(owner);
                    } else {
                        scheduler_task_set_state(owner, TASK_STATE_ZOMBIE, "reaper_wait_parent");
                        /* Зомби хранит статус выхода — и только его.
                         * Адресное пространство отдаётся смертью, не
                         * waitpid'ом: иначе жертва убийцы по полосам не
                         * освобождает ни страницы, пока родитель не
                         * соизволит спросить, — а родитель в этот момент
                         * сам стоит в отказе за памятью. */
                        vm_task_destroy(owner);
                        unix_proc_notify_waiters(owner->parent_task_id);
                    }
                }
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
    scheduler_set_bucket(reaper_thread, SCHED_BUCKET_BACKGROUND);
    scheduler_add_thread(reaper_thread);
}
