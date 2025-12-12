/**
 * @file scheduler.h
 * @brief Task scheduler interface
 * 
 * Provides task scheduling and context switching functionality.
 */

#ifndef _RODNIX_COMMON_SCHEDULER_H
#define _RODNIX_COMMON_SCHEDULER_H

#include "../core/task.h"
#include "../core/cpu.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Scheduler configuration
 * ============================================================================ */

#define SCHEDULER_MAX_PRIORITY 255
#define SCHEDULER_MIN_PRIORITY 0
#define SCHEDULER_DEFAULT_PRIORITY 128

/* Time slice in milliseconds */
#define SCHEDULER_TIME_SLICE_MS 10

/* ============================================================================
 * Scheduling policy
 * ============================================================================ */

typedef enum {
    SCHED_POLICY_FIFO = 0,     /* First In First Out */
    SCHED_POLICY_RR,          /* Round Robin */
    SCHED_POLICY_PRIORITY,    /* Priority-based */
    SCHED_POLICY_CFS,         /* Completely Fair Scheduler */
} sched_policy_t;

/* ============================================================================
 * Scheduler statistics
 * ============================================================================ */

typedef struct {
    uint64_t total_switches;   /* Total number of context switches */
    uint64_t total_tasks;      /* Total number of tasks created */
    uint64_t running_tasks;    /* Number of currently running tasks */
    uint64_t ready_tasks;      /* Number of ready tasks */
    uint64_t blocked_tasks;    /* Number of blocked tasks */
} scheduler_stats_t;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize the scheduler
 * @return 0 on success, negative value on error
 */
int scheduler_init(void);

/**
 * Start the scheduler
 * This function should be called after all initialization is complete
 */
void scheduler_start(void);

/* ============================================================================
 * Task management
 * ============================================================================ */

/**
 * Add a task to the scheduler
 * @param task Task to add
 * @return 0 on success, negative value on error
 */
int scheduler_add_task(task_t* task);

/**
 * Remove a task from the scheduler
 * @param task Task to remove
 * @return 0 on success, negative value on error
 */
int scheduler_remove_task(task_t* task);

/**
 * Get the currently running task
 * @return Pointer to current task or NULL
 */
task_t* scheduler_get_current_task(void);

/* ============================================================================
 * Thread management
 * ============================================================================ */

/**
 * Add a thread to the scheduler
 * @param thread Thread to add
 * @return 0 on success, negative value on error
 */
int scheduler_add_thread(thread_t* thread);

/**
 * Remove a thread from the scheduler
 * @param thread Thread to remove
 * @return 0 on success, negative value on error
 */
int scheduler_remove_thread(thread_t* thread);

/**
 * Get the currently running thread
 * @return Pointer to current thread or NULL
 */
thread_t* scheduler_get_current_thread(void);

/* ============================================================================
 * Scheduling control
 * ============================================================================ */

/**
 * Yield the CPU to another thread
 * Current thread will be moved to ready queue
 */
void scheduler_yield(void);

/**
 * Block the current thread
 * Thread will be removed from ready queue until unblocked
 */
void scheduler_block(void);

/**
 * Unblock a thread
 * @param thread Thread to unblock
 */
void scheduler_unblock(thread_t* thread);

/**
 * Sleep for a specified time
 * @param milliseconds Time to sleep in milliseconds
 */
void scheduler_sleep(uint64_t milliseconds);

/**
 * Set thread priority
 * @param thread Thread to modify
 * @param priority New priority (0-255)
 */
void scheduler_set_priority(thread_t* thread, uint8_t priority);

/**
 * Set scheduling policy
 * @param policy Scheduling policy to use
 * @return 0 on success, negative value on error
 */
int scheduler_set_policy(sched_policy_t policy);

/* ============================================================================
 * Timer tick handler
 * ============================================================================ */

/**
 * Handle timer tick
 * Should be called from timer interrupt handler
 */
void scheduler_tick(void);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * Get scheduler statistics
 * @param stats Pointer to structure to fill
 * @return 0 on success, negative value on error
 */
int scheduler_get_stats(scheduler_stats_t* stats);

#endif /* _RODNIX_COMMON_SCHEDULER_H */

