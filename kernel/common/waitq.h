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

#endif /* _RODNIX_COMMON_WAITQ_H */
