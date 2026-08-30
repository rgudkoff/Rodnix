/**
 * @file hygiene.h
 * @brief Bounds on how long a processor may be unavailable.
 *
 * The lock-order verifier answers "is the code built correctly". This answers
 * a different question, and for this system the more important one: "did we
 * make the deadline".
 *
 * RodNIX is for people making music and video. At 48 kHz with a 64-sample
 * buffer the period is 1.33 ms, and one missed deadline is an audible click.
 * A processor that spends 3 ms with interrupts masked has missed it -- and
 * nothing else in the kernel would have said so. No lock was held wrongly, no
 * order was reversed, nothing deadlocked. The code was correct and the product
 * was broken.
 *
 * So the thing to measure is the window itself: how long interrupts stayed
 * masked, how long preemption stayed disabled, how long an interrupt handler
 * ran. This is XNU's sched hygiene (osfmk/kern/sched_hygiene.h), and on a
 * phone it is not a warning -- interrupt_masked_debug_mode defaults to
 * SCHED_HYGIENE_MODE_PANIC with a 500 us threshold. A kernel that cannot keep
 * that promise is stopped rather than shipped.
 *
 * Note the asymmetry with WITNESS, which is the reason both exist:
 *
 *   - a deadlock is caught by hygiene too, as a window that never ends;
 *   - a 3 ms window that blocked nobody is invisible to WITNESS entirely.
 *
 * Gross and net time are tracked separately, because an interrupt landing in
 * the middle of a preemption-disabled window is not that window's fault.
 * Charging it would produce false reports on exactly the busy machines where
 * the numbers matter. XNU checks the net duration a second time before it
 * panics; so does this.
 */

#ifndef _RODNIX_CORE_HYGIENE_H
#define _RODNIX_CORE_HYGIENE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HYGIENE_OFF = 0,
    HYGIENE_TRACE,   /* record the worst, report a bounded number of them */
    HYGIENE_PANIC,   /* stop on the first window over threshold */
} hygiene_mode_t;

/*
 * rdnx.hygiene=off|trace|panic, and the thresholds in microseconds:
 *   rdnx.hygiene.int=<us>      interrupts masked      (default 500)
 *   rdnx.hygiene.preempt=<us>  preemption disabled    (default 2000)
 *   rdnx.hygiene.irq=<us>      one interrupt handler  (default 500)
 *
 * Default is trace, not panic: we do not yet know what our own numbers are,
 * and a threshold picked before the first measurement would only be a guess
 * that stops the machine. Panic is what it should become once the numbers are
 * known and defended.
 *
 * Needs a calibrated TSC. Without one the thresholds cannot be expressed in
 * time at all, so hygiene stays off and says so rather than measuring in units
 * that mean nothing.
 */
void hygiene_init(void);

/*
 * Read on every preempt_disable, so it is a plain global rather than a call.
 * The window bookkeeping behind it is not on the fast path -- this branch is
 * what keeps it off.
 */
extern bool g_hygiene_on;

static inline bool hygiene_enabled(void)
{
    return g_hygiene_on;
}

/* Interrupts masked. Call only for the outermost mask -- the one that found
 * interrupts enabled -- since a nested cli extends no window. */
void hygiene_int_begin(const char* site, int line);
void hygiene_int_end(void);

/* Preemption disabled. Outermost only: count 0 -> 1 and 1 -> 0. The site is
 * the lock that closed the window, which is the only useful name for it --
 * the count itself lives in one inline that every caller shares. */
void hygiene_preempt_begin(const char* site, int line);
void hygiene_preempt_end(void);
/* A lock taken while this cpu's window was already open; the first such
 * site is named in an over-limit report to separate "one long critical
 * section" from "a count that never came back to zero". */
void hygiene_preempt_nested(const char* site, int line);
/* Called from the tick with the interrupted RIP; samples it once per
 * preempt window, and only after the window has outlived the threshold. */
void hygiene_preempt_tick_probe(uint64_t rip, uint32_t preempt_count);

/* One interrupt, entry to exit. Also the source of the per-CPU handler time
 * that the other two windows subtract. */
void hygiene_irq_enter(uint32_t vector);
void hygiene_irq_exit(uint32_t vector);

/* Time this processor has spent in interrupt handlers, in TSC ticks. Read by
 * the spin timeout so a contended lock is not blamed for interrupts that
 * arrived while it was waiting. */
uint64_t hygiene_irq_ticks(void);

/* Worst windows seen, per processor. */
void hygiene_report(void);

#endif /* _RODNIX_CORE_HYGIENE_H */
