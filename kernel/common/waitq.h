/**
 * @file waitq.h
 * @brief Generic wait queue primitives for blocked threads
 */

#ifndef _RODNIX_COMMON_WAITQ_H
#define _RODNIX_COMMON_WAITQ_H

#include "../core/task.h"
#include "../../include/bsd/sys/queue.h"
#include <stdbool.h>
#include <stdint.h>

TAILQ_HEAD(waitq_thread_head, thread);

typedef struct waitq {
    struct waitq_thread_head threads;
    const char* name;
    uint32_t count;
} waitq_t;

void waitq_init(waitq_t* q, const char* name);
bool waitq_contains(const waitq_t* q, const thread_t* t);
int waitq_enqueue(waitq_t* q, thread_t* t);
int waitq_remove(waitq_t* q, thread_t* t);
thread_t* waitq_dequeue(waitq_t* q);
thread_t* waitq_wake_one(waitq_t* q);
uint32_t waitq_wake_all(waitq_t* q);
uint32_t waitq_count(const waitq_t* q);
int waitq_wait_until(waitq_t* q, uint64_t deadline_ticks);
int waitq_wait(waitq_t* q, uint64_t timeout_ms);
void waitq_tick(uint64_t now_ticks);
uint32_t waitq_timed_count(void);

#endif /* _RODNIX_COMMON_WAITQ_H */
