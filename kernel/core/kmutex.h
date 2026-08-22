/**
 * @file kmutex.h
 * @brief Sleeping mutex.
 *
 * The primitive the kernel was missing. Until now there was exactly one --
 * the spinlock -- so every long critical section had to choose between
 * holding interrupts off across block I/O and protecting nothing at all.
 * That is the position ext2 was in, and it is not a position that has a
 * right answer.
 *
 * A contending thread blocks on a wait queue instead of spinning, so a
 * holder is free to sleep: to wait on a disk, to allocate, to do anything
 * that yields. That is the whole point, and it is what separates this from
 * spinlock_lock(), whose holder may only be protected from preemption.
 *
 * Which to use:
 *   spinlock_lock_irqsave  short, and reachable from an interrupt handler
 *   spinlock_lock          short-to-medium, thread context, no sleeping
 *   kmutex                 anything that may sleep while holding it
 *
 * Two rules, both enforced rather than documented and hoped for:
 *
 *   - Never from interrupt context. There is no thread to put on the queue,
 *     and an interrupt handler cannot afford to wait for one that is.
 *   - Never while holding a spinlock. Sleeping with a spinlock held stops
 *     every processor that wants it, for as long as the sleep lasts. This is
 *     the rule FreeBSD states as "Giant must be released when blocking on a
 *     sleepable lock", generalised to any spinlock.
 *
 * Recursion is opt-in per mutex. Fine-grained locks should refuse it -- a
 * recursive acquire there is almost always a bug -- but a lock covering a
 * whole subsystem cannot work without it.
 */

#ifndef _RODNIX_CORE_KMUTEX_H
#define _RODNIX_CORE_KMUTEX_H

#include "task.h"
#include "../fabric/spin.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct kmutex {
    /* Guards every field below, including the wait queue. Held only for the
     * few instructions it takes to decide whether to sleep. */
    spinlock_t guard;

    thread_t* owner;         /* NULL when free */
    uint32_t depth;          /* recursion depth; 0 when free */
    bool recursive;          /* may the owner take it again */
    const char* name;        /* for diagnostics and panic messages */

    waitq_t waiters;
} kmutex_t;

void kmutex_init(kmutex_t* m, const char* name);

/* As kmutex_init, but the owner may re-acquire. Only for locks that cover
 * enough ground that recursion is unavoidable. */
void kmutex_init_recursive(kmutex_t* m, const char* name);

void kmutex_lock(kmutex_t* m);
void kmutex_unlock(kmutex_t* m);

/* Never sleeps; returns false rather than blocking. Safe anywhere a plain
 * memory access is safe, including interrupt context. */
bool kmutex_trylock(kmutex_t* m);

/* True if the calling thread holds it. For assertions in code that documents
 * "caller must hold". */
bool kmutex_owned(const kmutex_t* m);

#endif /* _RODNIX_CORE_KMUTEX_H */
