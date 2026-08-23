/**
 * @file witness.h
 * @brief Lock-order verifier.
 *
 * The self-deadlock detector in spin.c answers "did this processor take this
 * lock twice". That is one lock. The failure this file is about needs two:
 * one path takes A then B, another takes B then A, and the moment both run
 * at once neither ever finishes. Nothing about either path is wrong on its
 * own, which is why reading the code does not find it -- there is nothing to
 * see at either site.
 *
 * It also does not reproduce. Whether it fires depends on when the timer
 * interrupt lands, so a build can pass a hundred times and wedge on the
 * hundred and first, on a machine that is not yours.
 *
 * So the order has to be checked rather than trusted, and the check has to
 * run on orders that did *not* deadlock this time. That is what this does:
 * on every acquire, against every lock this processor already holds, record
 * the order and look for the reverse. A reversal is reported the first time
 * the two orders are both *seen* -- which does not require them to have
 * raced.
 *
 * The graph is learned, not declared. FreeBSD declares its order up front in
 * order_lists[] (sys/kern/subr_witness.c:503) because it knows what the order
 * is; we do not, and writing thirty-seven locks down from memory would only
 * produce a document to be wrong in. Learning it means the first runs also
 * answer "what is our lock order", which no one here can currently say.
 *
 * Scope: spinlocks. The sleeping mutex is deliberately out, and not from
 * laziness -- there is exactly one (Giant), so its graph is a single node
 * with nothing to reverse against. The cross-class rule that would matter,
 * "no sleeping lock under a spinlock", is already enforced where it is
 * cheapest to enforce: kmutex_lock() panics on it. When a second kmutex
 * appears, it wants a per-thread held-set here, because a mutex holder may
 * sleep and wake on another processor -- the per-CPU set below cannot follow
 * it. That is the same split FreeBSD makes.
 */

#ifndef _RODNIX_CORE_WITNESS_H
#define _RODNIX_CORE_WITNESS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WITNESS_OFF = 0,   /* no tracking at all */
    WITNESS_WARN,      /* report each reversed pair once, keep running */
    WITNESS_PANIC,     /* stop on the first reversal */
} witness_mode_t;

/* Reads rdnx.witness=off|warn|panic from the command line. Default warn:
 * a reversal is worth stopping for, but a false one from a pair that is in
 * truth ordered by something witness cannot see should not cost a boot. */
void witness_init(void);
witness_mode_t witness_mode(void);

/*
 * Check, before `name` is acquired, that taking it under everything this
 * processor already holds does not close a cycle. Returns the lock's node id,
 * which the caller caches so the next acquire skips the name lookup.
 *
 * Must be called with this processor pinned -- after preempt_disable() or
 * after cli -- because the held-set it consults is per-CPU.
 */
uint32_t witness_check(uint32_t* cached_id, const char* name,
                       const char* file, int line);

/* Record that the lock is now held. Separate from the check so a trylock,
 * which cannot deadlock because it never waits, can record without checking. */
void witness_acquired(uint32_t* cached_id, const void* addr,
                      const char* name, const char* file, int line);

void witness_release(const void* addr);

/* What this processor holds right now, newest first. Printed by the spin
 * timeout, where it is the entire evidence of a deadlock. */
void witness_dump_held(void);

/* The learned order graph. */
void witness_dump_graph(void);

/* Reversals reported so far. The self-test asserts on this; a verifier that
 * silently does nothing and a kernel with no reversals produce identical
 * output otherwise. */
uint32_t witness_reversal_count(void);

/*
 * Prove the verifier works, on locks that exist only for this.
 *
 * Runs on rdnx.witness=selftest. Single-threaded on purpose: a reversal is
 * two orders both being *seen*, which does not require them to race, and a
 * check that needed a race to fire could not be relied on to fire.
 */
void witness_selftest(void);

/* One line: mode, node count, edge count. Printed at the end of boot so a
 * build always says on the wire whether checking is on and whether it saw
 * anything -- a verifier that is silently disabled is worse than none. */
void witness_summary(void);

#endif /* _RODNIX_CORE_WITNESS_H */
