/**
 * @file ktime_arch.c
 * @brief x86_64 time source: the TSC, and how its frequency is established.
 *
 * The chain is tried best first, and which link answered is reported, because
 * "1.001 GHz" and "1.001 GHz, measured, +-0.03%" are different claims.
 */

#include "../ktime_arch.h"
#include "../../../include/console.h"
#include <stdbool.h>
#include <stddef.h>

static inline uint64_t rdtsc_now(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t ktime_arch_counter(void)
{
    return rdtsc_now();
}

static inline void cpuid_at(uint32_t leaf, uint32_t sub, uint32_t r[4])
{
    __asm__ volatile ("cpuid"
                      : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                      : "a"(leaf), "c"(sub));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}

/* The 8254's input clock. Fixed by the hardware's definition, not by anything
 * we measure, which is the entire reason this is the reference. */
#define PIT_BASE_HZ 1193182u

/*
 * Measure the TSC against the 8254, polling channel 2's output rather than
 * taking its interrupt.
 *
 * Polling is the point. Calibrating against interrupt *delivery* is what put
 * our timer 20 % out: under emulation the interrupts arrive late and coalesce,
 * so the window is longer than the tick count says and every frequency derived
 * from it comes out high. Channel 2's OUT bit is readable from port 0x61 with
 * no interrupt controller in the path at all.
 */
static uint64_t tsc_hz_from_pit(uint32_t window_ms)
{
    uint32_t divisor = (uint32_t)(((uint64_t)PIT_BASE_HZ * window_ms) / 1000u);
    if (divisor == 0 || divisor > 0xFFFFu) {
        return 0;
    }

    uint8_t p61 = inb(0x61);
    outb(0x61, (uint8_t)(p61 & 0xFCu));      /* gate low, speaker off */
    outb(0x43, 0xB0u);                       /* ch2, lobyte/hibyte, mode 0 */
    outb(0x42, (uint8_t)(divisor & 0xFFu));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFFu));

    uint64_t t0 = rdtsc_now();
    outb(0x61, (uint8_t)((p61 & 0xFCu) | 0x01u));   /* gate high: start */

    uint64_t guard = 0;
    while ((inb(0x61) & 0x20u) == 0u) {
        __asm__ volatile ("pause");
        if (++guard > 2000000000ULL) {
            outb(0x61, p61);
            return 0;                        /* the 8254 is not counting */
        }
    }
    uint64_t t1 = rdtsc_now();
    outb(0x61, p61);

    /* The count was `divisor` ticks of a PIT_BASE_HZ clock. */
    return ((t1 - t0) * (uint64_t)PIT_BASE_HZ) / (uint64_t)divisor;
}

uint64_t ktime_arch_calibrate(const char** how)
{
    uint32_t r[4];

    /*
     * A hypervisor that publishes the frequency is telling us exactly, and no
     * measurement of ours will beat it. FreeBSD treats this leaf as exact
     * (tsc_early_calib_exact) and so do we. QEMU under TCG does not offer it;
     * KVM does.
     */
    cpuid_at(0x40000000u, 0, r);
    if (r[0] >= 0x40000010u) {
        cpuid_at(0x40000010u, 0, r);
        if (r[0] != 0) {
            if (how) {
                *how = "TSC (hypervisor, exact)";
            }
            return (uint64_t)r[0] * 1000ULL;
        }
    }

    /* CPUID 0x15: core crystal frequency and the TSC's ratio to it. Exact when
     * all three fields are populated, which on many parts they are not. */
    cpuid_at(0, 0, r);
    uint32_t max_leaf = r[0];
    if (max_leaf >= 0x15u) {
        cpuid_at(0x15u, 0, r);
        if (r[0] != 0 && r[1] != 0 && r[2] != 0) {
            if (how) {
                *how = "TSC (CPUID 0x15, exact)";
            }
            return ((uint64_t)r[2] * r[1]) / r[0];
        }
    }

    /*
     * Measured against the 8254. Twice, over different windows, and only
     * accepted if the two agree: a single measurement has nothing to disagree
     * with, and that is how a wrong one survives.
     */
    uint64_t a = tsc_hz_from_pit(10u);
    uint64_t b = tsc_hz_from_pit(50u);
    if (a != 0 && b != 0) {
        uint64_t lo = (a < b) ? a : b;
        uint64_t hi = (a < b) ? b : a;
        if ((hi - lo) * 100u <= hi) {          /* within 1 % of each other */
            if (how) {
                *how = "TSC (measured against the 8254)";
            }
            return b;                           /* the longer window */
        }
        kprintf("[ktime] 8254 calibration disagrees with itself: "
                "%lluHz then %lluHz — the counter may not be steady\n",
                (unsigned long long)a, (unsigned long long)b);
        if (how) {
            *how = "TSC (measured, unsteady)";
        }
        return b;
    }

    /* CPUID 0x16 reports the nominal base frequency. The SDM calls it
     * informational, and it says nothing about the rate the TSC actually
     * counts at, so it is the last resort rather than a shortcut. */
    if (max_leaf >= 0x16u) {
        cpuid_at(0x16u, 0, r);
        if (r[0] != 0) {
            if (how) {
                *how = "TSC (CPUID 0x16, nominal only)";
            }
            return (uint64_t)r[0] * 1000000ULL;
        }
    }

    if (how) {
        *how = "none";
    }
    return 0;
}
