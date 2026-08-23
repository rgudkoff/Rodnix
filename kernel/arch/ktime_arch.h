/**
 * @file ktime_arch.h
 * @brief What the architecture owes the time subsystem.
 *
 * Two things, and deliberately only two: a counter, and an honest answer about
 * how fast it runs. Everything else -- scaling, verification, the API the rest
 * of the kernel uses -- is machine independent and lives in ktime.c.
 */

#ifndef _RODNIX_ARCH_KTIME_H
#define _RODNIX_ARCH_KTIME_H

#include <stdint.h>

/* A monotonically increasing counter. Must not stop, must not go backwards on
 * one processor, and should cost about as much as a load. */
uint64_t ktime_arch_counter(void);

/*
 * Its frequency in Hz, and the name of how that was established -- so a report
 * can say "measured against the 8254" rather than leaving the reader to guess
 * whether the number was measured or asserted. Zero if there is no usable
 * counter.
 */
uint64_t ktime_arch_calibrate(const char** how);

#endif /* _RODNIX_ARCH_KTIME_H */
