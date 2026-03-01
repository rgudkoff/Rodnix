#ifndef _RODNIX_COMMON_SCHEDULER_INTERNAL_H
#define _RODNIX_COMMON_SCHEDULER_INTERNAL_H

#include "../scheduler.h"
#include "../../arch/x86_64/interrupt_frame.h"
#include <stdbool.h>
#include <stdint.h>

#define READY_QUEUE_LEVELS 3

#define BOOST_THRESHOLD_TICKS 5
#define BOOST_MAX 32
#define PENALTY_MAX 32
#define PENALTY_STEP_TICKS 4

#define REALTIME_QUANTUM_TICKS 1
#define TIMESHARE_PRIO_STEP    64
#define TIMESHARE_MAX_BONUS    3
#define CPU_BOUND_THRESHOLD    8
#define CPU_BOUND_EXTRA_TICKS  2

extern bool scheduler_initialized;
extern bool scheduler_running;
extern thread_t* current_thread;
extern sched_policy_t current_policy;
extern scheduler_stats_t stats;

extern volatile bool in_scheduler;
extern uint32_t ticks_until_preempt;
extern uint32_t ticks_per_slice;
extern volatile bool resched_pending;
extern uint64_t sched_ticks;

extern thread_t* ready_head[READY_QUEUE_LEVELS];
extern thread_t* ready_tail[READY_QUEUE_LEVELS];

extern scheduler_reap_stats_t reap_stats;

void scheduler_thread_set_state(thread_t* thread, thread_state_t new_state, const char* reason);
void scheduler_task_set_state(task_t* task, task_state_t new_state, const char* reason);

void ready_enqueue(thread_t* thread);
thread_t* ready_dequeue(void);
int ready_queue_index_for_thread(const thread_t* thread);

int clamp_dyn_priority(int value, int base);
int thread_effective_priority(const thread_t* thread);
void scheduler_reset_timeslice(const thread_t* thread);
void scheduler_update_tss(thread_t* thread);

uint32_t scheduler_reap_queue_len(void);
void scheduler_reap_enqueue(thread_t* dead_thread);
void scheduler_reap_dead_threads(void);
void scheduler_reaper_start(void);

#endif
