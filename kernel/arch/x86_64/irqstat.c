/**
 * @file irqstat.c
 * @brief Per-CPU interrupt counters.
 */

#include "irqstat.h"
#include "cpu_topology.h"

#define IRQSTAT_VECTORS 256

/* Roughly 128KB of .bss for the largest supported machine. It is NOLOAD, so
 * it costs nothing in the image, and a flat array keeps the dispatcher's
 * increment to one indexed store with no allocation to have gone wrong. */
static uint64_t g_counts[X86_64_MAX_CPUS][IRQSTAT_VECTORS];
static uint64_t g_unhandled[X86_64_MAX_CPUS];

/* Set once per vector so a listing can skip what has never fired. Written
 * without synchronisation on purpose: the only transition is false to true,
 * and a lost write costs a line in a report, not correctness. */
static bool g_seen[IRQSTAT_VECTORS];

/* Per vector rather than per CPU: the question it answers is about the line,
 * which is one thing however many processors see it. */
static uint64_t g_unhandled_streak[IRQSTAT_VECTORS];

void irqstat_count(uint32_t cpu, uint32_t vector, bool handled)
{
    if (cpu >= X86_64_MAX_CPUS || vector >= IRQSTAT_VECTORS) {
        return;
    }

    g_counts[cpu][vector]++;
    g_seen[vector] = true;

    if (handled) {
        g_unhandled_streak[vector] = 0;
    } else {
        g_unhandled[cpu]++;
        g_unhandled_streak[vector]++;
    }
}

uint64_t irqstat_unhandled_streak(uint32_t vector)
{
    return (vector < IRQSTAT_VECTORS) ? g_unhandled_streak[vector] : 0;
}

void irqstat_clear_streak(uint32_t vector)
{
    if (vector < IRQSTAT_VECTORS) {
        g_unhandled_streak[vector] = 0;
    }
}

uint64_t irqstat_get(uint32_t cpu, uint32_t vector)
{
    if (cpu >= X86_64_MAX_CPUS || vector >= IRQSTAT_VECTORS) {
        return 0;
    }
    return g_counts[cpu][vector];
}

uint64_t irqstat_get_total(uint32_t vector)
{
    if (vector >= IRQSTAT_VECTORS) {
        return 0;
    }

    uint64_t total = 0;
    for (uint32_t cpu = 0; cpu < X86_64_MAX_CPUS; cpu++) {
        total += g_counts[cpu][vector];
    }
    return total;
}

uint64_t irqstat_get_unhandled(uint32_t cpu)
{
    if (cpu >= X86_64_MAX_CPUS) {
        return 0;
    }
    return g_unhandled[cpu];
}

bool irqstat_vector_seen(uint32_t vector)
{
    return (vector < IRQSTAT_VECTORS) && g_seen[vector];
}
