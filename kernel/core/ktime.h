/**
 * @file ktime.h
 * @brief Monotonic time, in nanoseconds, from whatever counter the machine has.
 *
 * One place that answers "how long was that", so the answer does not depend on
 * which subsystem is asking or what it happens to know about the hardware.
 * Everything that measures duration -- the latency windows, the spin timeout,
 * sleeps, timeouts, scheduling -- reads from here.
 *
 * The problem this exists to solve is not conversion. It is that a clock which
 * is wrong is indistinguishable from one which is right, unless something
 * checks. Ours was wrong: the timer ran at 800 Hz having been asked for 1000,
 * for months, and the only reason anyone noticed was that two subsystems
 * disagreed by 25 % and the disagreement happened to be printed.
 *
 * So the design is a chain of trust, and it is stated rather than assumed:
 *
 *   1. A reference whose rate is fixed by definition and not by anybody's
 *      measurement. On a PC that is the 8254 at 1193182 Hz, polled through
 *      channel 2's gate -- polled, not by interrupt, because interrupt
 *      delivery is exactly what cannot be trusted to be prompt. This is the
 *      ground truth and nothing else is.
 *
 *   2. The counter we actually read -- the TSC -- calibrated against it.
 *
 *   3. Everything derived, and then *verified* against the reference again.
 *      A calibration that is never checked is a number, not a measurement.
 *
 * Under a hypervisor the chain can be shorter: some publish the counter
 * frequency exactly (CPUID leaf 0x40000010), and FreeBSD marks that case
 * tsc_early_calib_exact for good reason. Ours does not, so we measure.
 */

#ifndef _RODNIX_CORE_KTIME_H
#define _RODNIX_CORE_KTIME_H

#include <stdbool.h>
#include <stdint.h>

/* Establish the time source. Must run after the CPU is identified and before
 * anything measures a duration. */
void ktime_init(void);

bool ktime_ready(void);

/* Monotonic nanoseconds since ktime_init(). Safe from any context: no lock, no
 * allocation, and a bounded number of instructions. */
uint64_t ktime_ns(void);

/* The raw counter, for code that wants to take two samples and convert the
 * difference once. Cheaper than two ktime_ns() calls and the same answer. */
uint64_t ktime_raw(void);
uint64_t ktime_raw_to_ns(uint64_t ticks);
uint64_t ktime_ns_to_raw(uint64_t ns);

/* An absolute deadline `ms` from now, in the same units as ktime_ns(). Zero
 * means "no deadline" and is passed through, because that is what every caller
 * means by a zero timeout. */
uint64_t ktime_deadline_ms(uint64_t ms);

uint64_t ktime_hz(void);
const char* ktime_source(void);

/* How far the calibration was off when last checked against the reference, in
 * parts per million, and which way. Zero before the first check. */
int64_t ktime_last_error_ppm(void);

/*
 * Measure an event rate against the time source and report the error.
 *
 * `count` events observed over the interval between two ktime_raw() samples,
 * against `expected_hz`. This is how a timer's programmed rate is checked
 * against reality, which is the check whose absence let 800 Hz pass for 1000.
 * Returns the error in parts per million, positive when the observed rate is
 * high.
 */
int64_t ktime_check_rate(const char* what, uint64_t count,
                         uint64_t raw_start, uint64_t raw_end,
                         uint64_t expected_hz);

void ktime_report(void);

#endif /* _RODNIX_CORE_KTIME_H */
