#include "internal.h"
#include "../../../include/console.h"

void scheduler_debug_dump(void)
{
    kputs("[SCHED] ---- dump ----\n");
    kprintf("[SCHED] ticks=%llu running=%d ready=%llu blocked=%llu\n",
            (unsigned long long)sched_ticks,
            scheduler_running ? 1 : 0,
            (unsigned long long)stats.ready_tasks,
            (unsigned long long)stats.blocked_tasks);
    reap_stats.queue_len = scheduler_reap_queue_len();
    kprintf("[SCHED] reap qlen=%u hwm=%u enq=%llu reap=%llu def=%llu drop=%llu runs=%llu\n",
            (unsigned)reap_stats.queue_len,
            (unsigned)reap_stats.queue_hwm,
            (unsigned long long)reap_stats.enqueued,
            (unsigned long long)reap_stats.reaped,
            (unsigned long long)reap_stats.deferred,
            (unsigned long long)reap_stats.dropped,
            (unsigned long long)reap_stats.runs);
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
        thread_t* it = NULL;
        TAILQ_FOREACH(it, &ready_queues[q], sched_link) {
            count++;
            if (count >= 1024) {
                break;
            }
        }
        kprintf("[SCHED] q%d count=%u\n", q, count);
    }
    kputs("[SCHED] --------------\n");
}
