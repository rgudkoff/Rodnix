/**
 * @file ktime.c
 * @brief Monotonic time. See core/ktime.h.
 */

#include "core/ktime.h"
#include "arch/ktime_arch.h"
#include "../include/console.h"
#include "../include/debug.h"
#include <stddef.h>

#define KTIME_SCALE_SHIFT 32

static uint64_t g_hz;
static uint64_t g_scale;      /* ns per tick, in Q32 */
static uint64_t g_inv_scale;  /* ticks per ns, in Q32 */
static uint64_t g_origin;
static const char* g_source = "none";
static int64_t g_last_error_ppm;

void ktime_init(void)
{
    const char* how = NULL;
    uint64_t hz = ktime_arch_calibrate(&how);
    if (hz == 0) {
        kprintf("[ktime] no usable time source — durations are unmeasurable\n");
        return;
    }

    g_hz = hz;
    g_source = how ? how : "counter";

    /*
     * Scaling by multiply-and-shift rather than dividing on every read. The
     * error is under one part in 2^32, which is four orders of magnitude
     * below anything the hardware itself is good for, and it costs a multiply
     * where a 64-bit divide would cost tens of cycles on a path that runs on
     * every lock acquire.
     */
    /* 128-bit multiplication is a mulq; 128-bit division is a libgcc call we
     * do not link, so both constants are built in 64 bits. 1e9 << 32 is
     * 4.3e18 and fits; hz << 32 would not for a fast counter, so that one is
     * split into whole and fractional parts. */
    g_scale = (1000000000ULL << KTIME_SCALE_SHIFT) / hz;
    g_inv_scale = ((hz / 1000000000ULL) << KTIME_SCALE_SHIFT)
                  + (((hz % 1000000000ULL) << KTIME_SCALE_SHIFT) / 1000000000ULL);
    g_origin = ktime_arch_counter();
}

bool ktime_ready(void)
{
    return g_hz != 0;
}

uint64_t ktime_raw(void)
{
    return ktime_arch_counter();
}

uint64_t ktime_raw_to_ns(uint64_t ticks)
{
    if (!g_hz) {
        return 0;
    }
    return (uint64_t)(((__uint128_t)ticks * g_scale) >> KTIME_SCALE_SHIFT);
}

uint64_t ktime_ns_to_raw(uint64_t ns)
{
    if (!g_hz) {
        return 0;
    }
    return (uint64_t)(((__uint128_t)ns * g_inv_scale) >> KTIME_SCALE_SHIFT);
}

uint64_t ktime_ns(void)
{
    if (!g_hz) {
        return 0;
    }
    return ktime_raw_to_ns(ktime_arch_counter() - g_origin);
}

uint64_t ktime_deadline_ms(uint64_t ms)
{
    if (ms == 0) {
        return 0;
    }
    uint64_t d = ktime_ns() + ms * 1000000ULL;
    return d ? d : 1;   /* never collide with "no deadline" */
}

uint64_t ktime_hz(void)
{
    return g_hz;
}

const char* ktime_source(void)
{
    return g_source;
}

int64_t ktime_last_error_ppm(void)
{
    return g_last_error_ppm;
}

int64_t ktime_check_rate(const char* what, uint64_t count,
                         uint64_t raw_start, uint64_t raw_end,
                         uint64_t expected_hz)
{
    if (!g_hz || expected_hz == 0 || raw_end <= raw_start || count == 0) {
        return 0;
    }

    uint64_t elapsed_ns = ktime_raw_to_ns(raw_end - raw_start);
    if (elapsed_ns == 0) {
        return 0;
    }

    /* observed = count / elapsed; error = (observed - expected) / expected */
    uint64_t observed_hz = (count * 1000000000ULL) / elapsed_ns;
    int64_t diff = (int64_t)observed_hz - (int64_t)expected_hz;
    int64_t ppm = (diff * 1000000) / (int64_t)expected_hz;
    g_last_error_ppm = ppm;

    /*
     * A tenth of a percent is generous for anything derived from a crystal and
     * tight enough that the failure we actually had -- 200000 ppm -- could not
     * hide. Reported rather than fatal: a clock that is wrong is still better
     * than a machine that will not boot, provided it says so.
     */
    if (ppm > 1000 || ppm < -1000) {
        kprintf("[ktime] %s runs at %lluHz, asked for %lluHz (%lld ppm off)\n",
                what ? what : "rate",
                (unsigned long long)observed_hz,
                (unsigned long long)expected_hz,
                (long long)ppm);
    }
    return ppm;
}

void ktime_report(void)
{
    if (!g_hz) {
        kprintf("[ktime] unavailable\n");
        return;
    }
    kprintf("[ktime] %s at %llu.%03llu MHz, %llu ns resolution\n",
            g_source,
            (unsigned long long)(g_hz / 1000000ULL),
            (unsigned long long)((g_hz / 1000ULL) % 1000ULL),
            (unsigned long long)(g_hz ? (1000000000ULL + g_hz - 1) / g_hz : 0));
}
