/**
 * @file waitq.c
 * @brief Generic wait queue primitives for blocked threads
 */

#include "waitq.h"
#include "scheduler.h"
#include "../../include/error.h"
#include <stddef.h>

void waitq_init(waitq_t* q, const char* name)
{
    if (!q) {
        return;
    }
    TAILQ_INIT(&q->threads);
    q->name = name;
    q->count = 0;
}

bool waitq_contains(const waitq_t* q, const thread_t* t)
{
    if (!q || !t) {
        return false;
    }
    return t->waitq_owner == q;
}

int waitq_enqueue(waitq_t* q, thread_t* t)
{
    if (!q || !t) {
        return RDNX_E_INVALID;
    }
    if (t->waitq_owner == q) {
        return RDNX_E_BUSY;
    }
    if (t->waitq_owner != NULL) {
        return RDNX_E_BUSY;
    }

    TAILQ_INSERT_TAIL(&q->threads, t, wait_link);
    t->waitq_owner = q;
    q->count++;
    return RDNX_OK;
}

int waitq_remove(waitq_t* q, thread_t* t)
{
    if (!q || !t) {
        return RDNX_E_INVALID;
    }
    if (t->waitq_owner != q) {
        return RDNX_E_NOTFOUND;
    }

    TAILQ_REMOVE(&q->threads, t, wait_link);
    t->waitq_owner = NULL;
    if (q->count > 0) {
        q->count--;
    }
    return RDNX_OK;
}

thread_t* waitq_dequeue(waitq_t* q)
{
    if (!q) {
        return NULL;
    }
    thread_t* t = TAILQ_FIRST(&q->threads);
    if (!t) {
        return NULL;
    }

    TAILQ_REMOVE(&q->threads, t, wait_link);
    t->waitq_owner = NULL;
    if (q->count > 0) {
        q->count--;
    }
    return t;
}

thread_t* waitq_wake_one(waitq_t* q)
{
    thread_t* t = waitq_dequeue(q);
    if (!t) {
        return NULL;
    }
    scheduler_wake(t);
    return t;
}

uint32_t waitq_wake_all(waitq_t* q)
{
    if (!q) {
        return 0;
    }
    uint32_t woke = 0;
    while (waitq_wake_one(q)) {
        woke++;
    }
    return woke;
}

uint32_t waitq_count(const waitq_t* q)
{
    return q ? q->count : 0;
}
