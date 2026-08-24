#include "internal.h"
#include "../kernel/core/interrupts.h"
#include "../include/debug.h"
#include "../include/error.h"

bool scheduler_initialized = false;
bool scheduler_running = false;
sched_policy_t current_policy = SCHED_POLICY_PRIORITY;
scheduler_stats_t stats = {0};

uint32_t ticks_per_slice = 1;
uint64_t sched_ticks = 0;

struct ready_queue_head ready_queues[READY_QUEUE_LEVELS];

scheduler_reap_stats_t reap_stats = {0};
waitq_t scheduler_sleep_waitq;
waitq_t scheduler_join_waitq;
uint64_t bucket_last_run_tick[READY_QUEUE_LEVELS] = {0};


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

/*
 * Перевод потока в состояние, выраженное прежним перечислением.
 *
 * Остаётся только для мест, которым действительно нужно объявить намерение
 * целиком: «этот поток теперь мёртв», «этот поток теперь готов». Пути сна и
 * пробуждения сюда не ходят — их переходы совмещены с решением о постановке
 * в очередь и живут в scheduler_wake() и в арбитре switch_from_irq(), под
 * тем же sched_lock. */
void scheduler_thread_set_state(thread_t* thread, thread_state_t new_state, const char* reason)
{
    (void)reason;
    if (!thread) {
        return;
    }

    uint32_t set = 0;
    uint32_t clear = 0;
    switch (new_state) {
    case THREAD_STATE_NEW:
        clear = TH_RUN | TH_WAIT | TH_DEAD | TH_BLOCK;
        break;
    case THREAD_STATE_READY:
    case THREAD_STATE_RUNNING:
        /* Один бит на оба: чем поток занят прямо сейчас, знает процессор, а
         * не поле состояния. Так у XNU, и по той же причине. */
        set = TH_RUN;
        clear = TH_WAIT | TH_BLOCK;
        break;
    case THREAD_STATE_BLOCKED:
    case THREAD_STATE_SLEEPING:
        set = TH_WAIT;
        clear = TH_RUN;
        break;
    case THREAD_STATE_DEAD:
        set = TH_DEAD;
        clear = TH_RUN | TH_WAIT | TH_BLOCK;
        break;
    default:
        return;
    }

    /* Обычная запись под замком потока, не CAS. Атомарность одного поля
     * здесь ничего не гарантирует — инварианты составные (state плюс
     * членство в очередях), и их держит sched_lock, как thread_lock у XNU. */
    uint64_t f = spinlock_lock_irqsave(&thread->sched_lock);
    uint32_t old = thread->state;
    if ((old & TH_DEAD) && new_state != THREAD_STATE_DEAD) {
        spinlock_unlock_irqrestore(&thread->sched_lock, f);
        return;   /* мёртвый поток не оживает */
    }
    thread->state = (old | set) & ~clear;
    spinlock_unlock_irqrestore(&thread->sched_lock, f);
}

const char* thread_state_name(const thread_t* t)
{
    if (!t) {
        return "none";
    }
    uint32_t s = thread_state_get(t);
    if (s & TH_DEAD) {
        return "dead";
    }
    if (s & TH_IDLE) {
        return "idle";
    }
    if (s & TH_WAIT) {
        return (s & TH_RUN) ? "waking" : "waiting";
    }
    if (s & TH_RUN) {
        return "runnable";
    }
    return "new";
}

thread_state_t thread_state_legacy(const thread_t* t)
{
    if (!t) {
        return THREAD_STATE_NEW;
    }
    uint32_t s = thread_state_get(t);
    if (s & TH_DEAD) {
        return THREAD_STATE_DEAD;
    }
    if (s & TH_WAIT) {
        return THREAD_STATE_BLOCKED;
    }
    if (s & TH_RUN) {
        return (t == thread_get_current()) ? THREAD_STATE_RUNNING
                                           : THREAD_STATE_READY;
    }
    return THREAD_STATE_NEW;
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
        bucket_last_run_tick[i] = 0;
    }
    waitq_init(&scheduler_sleep_waitq, "scheduler_sleep");
    waitq_init(&scheduler_join_waitq, "scheduler_join");
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
    interrupt_trigger_resched();
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
    if (thread_is_dead(thread) || thread_is_waiting(thread)) {
        DEBUG_WARN("add_thread: thread %llu state=%s",
                   (unsigned long long)thread->thread_id,
                   thread_state_name(thread));
    }
    scheduler_thread_set_state(thread, THREAD_STATE_READY, "scheduler_add_thread");
    thread->base_priority = thread->priority;
    thread->dyn_priority = thread->priority;
    thread->inherited_priority = thread->priority;
    thread->has_inherited = 0;
    /* Назначить DEFAULT-бакет если не был выставлен явно */
    if (!thread->sched_bucket_explicit) {
        thread->sched_bucket = (uint8_t)SCHED_BUCKET_DEFAULT;
    }
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

void scheduler_mark_runnable_unqueued(thread_t* thread)
{
    if (!thread) {
        return;
    }
    scheduler_thread_set_state(thread, THREAD_STATE_READY, "idle_unqueued");
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
