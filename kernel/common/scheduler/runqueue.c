#include "internal.h"
#include "../../arch/gdt.h"
#include "../../../include/debug.h"

void scheduler_update_tss(thread_t* thread)
{
    if (!thread || !thread->stack || thread->stack_size == 0) {
        return;
    }
    uint64_t rsp0 = (uint64_t)(uintptr_t)thread->stack + thread->stack_size - 16;
    tss_set_rsp0(rsp0);
}

int clamp_dyn_priority(int value, int base)
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

int thread_effective_priority(const thread_t* thread)
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

void scheduler_reset_timeslice(const thread_t* thread)
{
    if (!thread) {
        ticks_until_preempt = ticks_per_slice;
        return;
    }
    if (thread->sched_class == SCHED_CLASS_REALTIME) {
        ticks_until_preempt = REALTIME_QUANTUM_TICKS;
        return;
    }
    uint32_t base = ticks_per_slice;
    if (base == 0) {
        base = 1;
    }
    /* Квант определяется бакетом: INTERACTIVE — короткий (отзывчивость),
     * BACKGROUND — длинный (меньше переключений). */
    static const uint32_t bucket_mult[SCHED_BUCKET_COUNT] = {
        BUCKET_QUANTUM_BACKGROUND,
        BUCKET_QUANTUM_UTILITY,
        BUCKET_QUANTUM_DEFAULT,
        BUCKET_QUANTUM_INTERACTIVE,
    };
    uint8_t bucket = thread->sched_bucket;
    if (bucket >= SCHED_BUCKET_COUNT) {
        bucket = SCHED_BUCKET_DEFAULT;
    }
    ticks_until_preempt = base * bucket_mult[bucket];
    if (ticks_until_preempt == 0) {
        ticks_until_preempt = 1;
    }
}

bool ready_thread_is_queued(const thread_t* thread)
{
    return thread && thread->ready_queued != 0;
}

void ready_enqueue(thread_t* thread)
{
    if (!thread) {
        return;
    }

    if (ready_thread_is_queued(thread)) {
        DEBUG_WARN("ready_enqueue: thread %llu already queued", (unsigned long long)thread->thread_id);
        return;
    }
    int q = ready_queue_index_for_thread(thread);
    if (q < 0 || q >= READY_QUEUE_LEVELS) {
        q = (int)SCHED_BUCKET_DEFAULT;
    }
    TAILQ_INSERT_TAIL(&ready_queues[q], thread, sched_link);
    thread->ready_queued = 1;
    stats.ready_tasks++;
}

/* Вспомогательная функция: извлечь первый поток из очереди q и обновить метрики. */
static thread_t* dequeue_from(int q)
{
    struct ready_queue_head* queue = &ready_queues[q];
    thread_t* thread = TAILQ_FIRST(queue);
    if (!thread) {
        return NULL;
    }
    if (thread->state != THREAD_STATE_READY) {
        DEBUG_WARN("ready_dequeue: thread %llu state=%d",
                   (unsigned long long)thread->thread_id, thread->state);
    }
    TAILQ_REMOVE(queue, thread, sched_link);
    thread->sched_link.tqe_next = NULL;
    thread->sched_link.tqe_prev = NULL;
    thread->ready_queued = 0;
    if (stats.ready_tasks > 0) {
        stats.ready_tasks--;
    }
    bucket_last_run_tick[q] = sched_ticks;
    return thread;
}

thread_t* ready_dequeue(void)
{
    /* RR/FIFO: всё в DEFAULT-очереди */
    if (current_policy == SCHED_POLICY_RR || current_policy == SCHED_POLICY_FIFO) {
        return dequeue_from((int)SCHED_BUCKET_DEFAULT);
    }

    /* Starvation avoidance: если нижний бакет не получал CPU давно — дать ему слот.
     * Проверяем снизу вверх (BACKGROUND → UTILITY → DEFAULT), исключая INTERACTIVE —
     * он и так всегда имеет приоритет. */
    for (int b = (int)SCHED_BUCKET_BACKGROUND; b < (int)SCHED_BUCKET_INTERACTIVE; b++) {
        if (!TAILQ_EMPTY(&ready_queues[b]) &&
            (sched_ticks - bucket_last_run_tick[b]) >= STARVATION_THRESHOLD_TICKS) {
            return dequeue_from(b);
        }
    }

    /* Нормальный путь: выбрать из наиболее приоритетного непустого бакета */
    for (int q = READY_QUEUE_LEVELS - 1; q >= 0; q--) {
        thread_t* t = dequeue_from(q);
        if (t) {
            return t;
        }
    }

    return NULL;
}

int ready_queue_index_for_thread(const thread_t* thread)
{
    if (!thread) {
        return (int)SCHED_BUCKET_DEFAULT;
    }
    if (current_policy == SCHED_POLICY_RR || current_policy == SCHED_POLICY_FIFO) {
        return (int)SCHED_BUCKET_DEFAULT;
    }
    int bucket = (int)thread->sched_bucket;
    if (bucket < 0 || bucket >= READY_QUEUE_LEVELS) {
        return (int)SCHED_BUCKET_DEFAULT;
    }
    return bucket;
}
