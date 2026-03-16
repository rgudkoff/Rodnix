#ifndef _RODNIX_COMMON_SCHEDULER_INTERNAL_H
#define _RODNIX_COMMON_SCHEDULER_INTERNAL_H

#include "../scheduler.h"
#include "../waitq.h"
#include "../../arch/interrupt_frame.h"
#include "../../../include/bsd/sys/queue.h"
#include <stdbool.h>
#include <stdint.h>

/* Число очередей = число QoS-бакетов */
#define READY_QUEUE_LEVELS ((int)SCHED_BUCKET_COUNT)

#define BOOST_THRESHOLD_TICKS 5
#define BOOST_MAX 32
#define PENALTY_MAX 32
#define PENALTY_STEP_TICKS 4

#define REALTIME_QUANTUM_TICKS 1

/* Per-bucket quantum: множитель на ticks_per_slice.
 * INTERACTIVE — короче (отзывчивость), BACKGROUND — длиннее (меньше переключений). */
#define BUCKET_QUANTUM_INTERACTIVE 1
#define BUCKET_QUANTUM_DEFAULT     2
#define BUCKET_QUANTUM_UTILITY     4
#define BUCKET_QUANTUM_BACKGROUND  8

/* Starvation avoidance: если бакет не получал CPU N тиков, он получает внеочередной слот. */
#define STARVATION_THRESHOLD_TICKS 500

extern bool scheduler_initialized;
extern bool scheduler_running;
extern sched_policy_t current_policy;
extern scheduler_stats_t stats;

extern volatile bool in_scheduler;
extern uint32_t ticks_until_preempt;
extern uint32_t ticks_per_slice;
extern volatile bool resched_pending;
extern uint64_t sched_ticks;

TAILQ_HEAD(ready_queue_head, thread);
extern struct ready_queue_head ready_queues[READY_QUEUE_LEVELS];

extern scheduler_reap_stats_t reap_stats;
extern waitq_t scheduler_sleep_waitq;
extern uint64_t bucket_last_run_tick[READY_QUEUE_LEVELS]; /* последний тик каждого бакета */

void scheduler_thread_set_state(thread_t* thread, thread_state_t new_state, const char* reason);
void scheduler_task_set_state(task_t* task, task_state_t new_state, const char* reason);

void ready_enqueue(thread_t* thread);
thread_t* ready_dequeue(void);
int ready_queue_index_for_thread(const thread_t* thread);
bool ready_thread_is_queued(const thread_t* thread);

int clamp_dyn_priority(int value, int base);
int thread_effective_priority(const thread_t* thread);
void scheduler_reset_timeslice(const thread_t* thread);
void scheduler_update_tss(thread_t* thread);

uint32_t scheduler_reap_queue_len(void);
void scheduler_reap_enqueue(thread_t* dead_thread);
void scheduler_reap_dead_threads(void);
void scheduler_reaper_start(void);

#endif
