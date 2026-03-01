/**
 * @file waitq.c
 * @brief Generic wait queue primitives for blocked threads
 */

#include "waitq.h"
#include "scheduler.h"
#include "../../include/error.h"
#include <stddef.h>

TAILQ_HEAD(waitq_timeout_head, thread);
static struct waitq_timeout_head waitq_timeouts;
static bool waitq_timeouts_initialized = false;

static void waitq_timeouts_init_once(void)
{
    if (waitq_timeouts_initialized) {
        return;
    }
    TAILQ_INIT(&waitq_timeouts);
    waitq_timeouts_initialized = true;
}

static void waitq_disarm_timeout(thread_t* t)
{
    if (!t || !t->wait_timeout_armed) {
        return;
    }
    TAILQ_REMOVE(&waitq_timeouts, t, wait_timeout_link);
    t->wait_timeout_armed = 0;
    t->wait_deadline_tick = 0;
    t->wait_timeout_link.tqe_next = NULL;
    t->wait_timeout_link.tqe_prev = NULL;
}

static void waitq_arm_timeout(thread_t* t, uint64_t deadline_ticks)
{
    if (!t || deadline_ticks == 0) {
        return;
    }
    waitq_timeouts_init_once();
    if (t->wait_timeout_armed) {
        waitq_disarm_timeout(t);
    }
    t->wait_deadline_tick = deadline_ticks;
    TAILQ_INSERT_TAIL(&waitq_timeouts, t, wait_timeout_link);
    t->wait_timeout_armed = 1;
}

static uint64_t waitq_deadline_from_timeout_ms(uint64_t timeout_ms)
{
    if (timeout_ms == 0) {
        return 0;
    }
    uint64_t now = scheduler_get_ticks();
    uint64_t ticks = (timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1)) / SCHEDULER_TIME_SLICE_MS;
    if (ticks == 0) {
        ticks = 1;
    }
    return now + ticks;
}

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
    t->wait_timed_out = 0;
    waitq_disarm_timeout(t);
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
    t->wait_timed_out = 0;
    waitq_disarm_timeout(t);
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

uint32_t waitq_timed_count(void)
{
    if (!waitq_timeouts_initialized) {
        return 0;
    }
    uint32_t count = 0;
    thread_t* it = NULL;
    TAILQ_FOREACH(it, &waitq_timeouts, wait_timeout_link) {
        count++;
    }
    return count;
}

int waitq_wait_until(waitq_t* q, uint64_t deadline_ticks)
{
    if (!q) {
        return RDNX_E_INVALID;
    }

    thread_t* self = thread_get_current();
    if (!self) {
        return RDNX_E_INVALID;
    }

    self->wait_timed_out = 0;
    if (!waitq_contains(q, self)) {
        int qret = waitq_enqueue(q, self);
        if (qret != RDNX_OK && qret != RDNX_E_BUSY) {
            return qret;
        }
    }
    if (deadline_ticks) {
        waitq_arm_timeout(self, deadline_ticks);
    }

    while (waitq_contains(q, self)) {
        scheduler_block();
        /* Trigger immediate dispatch to avoid spinning in current context. */
        __asm__ volatile ("int $32");
    }

    int ret = self->wait_timed_out ? RDNX_E_TIMEOUT : RDNX_OK;
    self->wait_timed_out = 0;
    return ret;
}

int waitq_wait(waitq_t* q, uint64_t timeout_ms)
{
    return waitq_wait_until(q, waitq_deadline_from_timeout_ms(timeout_ms));
}

void waitq_tick(uint64_t now_ticks)
{
    waitq_timeouts_init_once();
    thread_t* it = NULL;
    thread_t* next = NULL;
    TAILQ_FOREACH_SAFE(it, &waitq_timeouts, wait_timeout_link, next) {
        if (!it->wait_timeout_armed || it->wait_deadline_tick == 0) {
            waitq_disarm_timeout(it);
            continue;
        }
        if (it->wait_deadline_tick > now_ticks) {
            continue;
        }

        waitq_t* owner = it->waitq_owner;
        it->wait_timed_out = 1;
        waitq_disarm_timeout(it);
        if (owner && waitq_contains(owner, it)) {
            TAILQ_REMOVE(&owner->threads, it, wait_link);
            it->waitq_owner = NULL;
            if (owner->count > 0) {
                owner->count--;
            }
        }
        scheduler_wake(it);
    }
}
