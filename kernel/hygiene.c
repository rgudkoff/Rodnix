/**
 * @file hygiene.c
 * @brief Bounds on how long a processor may be unavailable. See core/hygiene.h.
 */

#include "core/hygiene.h"
#include "core/boot.h"
#include "core/cpu.h"
#include "arch/percpu.h"
#include "../include/console.h"
#include "../include/debug.h"
#include <stddef.h>

#define HYGIENE_MAX_CPUS 64

/*
 * Above this, a window is not believed.
 *
 * Nothing in this kernel legitimately holds a processor for a second: the spin
 * timeout panics at one, the longest real window measured is single-digit
 * milliseconds, and a genuine second-long stall would be a deadlock, which has
 * its own detector. So a duration past this bound says the clock moved without
 * the machine moving, and the honest thing is to drop it rather than report
 * the largest number the emulator happened to produce.
 */
#define HYGIENE_IMPLAUSIBLE_US 1000000ULL

/* How many over-threshold windows are printed before the mechanism goes quiet
 * and only keeps counting. A latency bug that fires once a millisecond would
 * otherwise be reported by drowning the evidence of everything else. */
#define HYGIENE_REPORT_BUDGET 8

/* Raise with rdnx.hygiene.reports=N when the question is which sites offend
 * rather than whether any do. The default is small because the streamed lines
 * are for noticing, and the aggregate report is for reading. */

struct hygiene_window {
    uint64_t worst_gross;
    uint64_t worst_net;
    const char* worst_site;
    int worst_line;
    uint32_t worst_aux;      /* vector, for the interrupt window */
    uint64_t over_count;
    uint64_t total_count;
    /* Windows found still open when the next one started. A window is opened
     * and closed by a matched pair, so this can only be nonzero if the pair
     * was broken -- which makes every duration after it meaningless. Counted
     * rather than assumed absent: the first version of this file guarded the
     * bookkeeping with the reporting guard, so a window closing while a report
     * printed was never closed at all, and the next close charged it seconds
     * of unrelated time. The number looked like a finding. */
    uint64_t stale_count;
    /* Windows whose measured duration cannot be true. The clock can move
     * while the machine does not: under emulation the host may deschedule the
     * whole guest, and the TSC keeps counting through it. Measured here as
     * windows of twelve seconds during which the timer tick advanced by
     * exactly zero -- no instruction ran, so nothing was held for twelve
     * seconds. Folding those into the worst case would put a number in the
     * report that no code is responsible for. Discarded, and counted, because
     * a discarded measurement that nobody can see is indistinguishable from a
     * measurement that was never taken. */
    uint64_t implausible_count;
};

struct hygiene_cpu {
    /* Open windows. Zero means "not measuring": a window that starts exactly
     * at TSC 0 is not a case worth a separate flag for. */
    uint64_t int_start;
    const char* int_site;
    int int_line;

    uint64_t preempt_start;
    uint64_t preempt_irq_base;   /* irq_ticks when the window opened */

    uint64_t irq_start;
    uint32_t irq_vector;

    /* Time spent inside interrupt handlers on this processor. The other two
     * windows subtract the delta across themselves. */
    uint64_t irq_ticks;

    /* Reporting re-enters through kprintf, which masks interrupts and takes
     * a lock, which is another window. Same guard as witness, same reason. */
    uint32_t busy;

    struct hygiene_window w_int;
    struct hygiene_window w_preempt;
    struct hygiene_window w_irq;
} __attribute__((aligned(64)));

static struct hygiene_cpu g_hyg[HYGIENE_MAX_CPUS];

bool g_hygiene_on = false;
static hygiene_mode_t g_mode = HYGIENE_TRACE;
static uint64_t g_tsc_hz;

/* Thresholds in TSC ticks, converted once from the microsecond settings. */
static uint64_t g_thr_int;
static uint64_t g_thr_preempt;
static uint64_t g_thr_irq;

static uint32_t g_report_budget = HYGIENE_REPORT_BUDGET;

static inline struct hygiene_cpu* hyg_cpu(void)
{
    uint32_t i = percpu_index();
    if (i >= HYGIENE_MAX_CPUS) {
        i = 0;
    }
    return &g_hyg[i];
}

static inline uint64_t hyg_now(void)
{
    return cpu_get_time();
}

static uint64_t hyg_us(uint64_t ticks)
{
    if (!g_tsc_hz) {
        return 0;
    }
    return (ticks * 1000000ULL) / g_tsc_hz;
}

/* ---- command line ---------------------------------------------------- */

static bool hyg_prefix(const char* s, const char* pfx, const char** rest)
{
    while (*pfx) {
        if (*s != *pfx) {
            return false;
        }
        s++;
        pfx++;
    }
    *rest = s;
    return true;
}

static uint64_t hyg_atoi(const char* s)
{
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint64_t)(*s - '0');
        s++;
    }
    return v;
}

static uint64_t g_us_int = 500;      /* XNU's phone default for the same window */
static uint64_t g_us_preempt = 2000;
static uint64_t g_us_irq = 500;

void hygiene_init(void)
{
    const boot_info_t* bi = boot_get_info();
    const char* cmdline = (bi && bi->cmdline[0]) ? bi->cmdline : "";

    for (const char* p = cmdline; *p; p++) {
        const char* rest;
        if (hyg_prefix(p, "rdnx.hygiene.int=", &rest)) {
            g_us_int = hyg_atoi(rest);
        } else if (hyg_prefix(p, "rdnx.hygiene.preempt=", &rest)) {
            g_us_preempt = hyg_atoi(rest);
        } else if (hyg_prefix(p, "rdnx.hygiene.irq=", &rest)) {
            g_us_irq = hyg_atoi(rest);
        } else if (hyg_prefix(p, "rdnx.hygiene.reports=", &rest)) {
            g_report_budget = (uint32_t)hyg_atoi(rest);
        } else if (hyg_prefix(p, "rdnx.hygiene=", &rest)) {
            if (rest[0] == 'o' && rest[1] == 'f') {
                g_mode = HYGIENE_OFF;
            } else if (rest[0] == 'p') {
                g_mode = HYGIENE_PANIC;
            } else {
                g_mode = HYGIENE_TRACE;
            }
        }
    }

    g_tsc_hz = cpu_get_frequency();
    if (g_mode == HYGIENE_OFF) {
        return;
    }
    if (g_tsc_hz == 0) {
        /*
         * Without a calibrated TSC a threshold cannot be stated in time, and a
         * threshold in units nobody can name is worse than no threshold: it
         * would produce numbers that look like measurements.
         */
        kprintf("[hygiene] no calibrated TSC — latency windows unmeasured\n");
        return;
    }

    g_thr_int     = (g_us_int     * g_tsc_hz) / 1000000ULL;
    g_thr_preempt = (g_us_preempt * g_tsc_hz) / 1000000ULL;
    g_thr_irq     = (g_us_irq     * g_tsc_hz) / 1000000ULL;

    g_hygiene_on = true;
}

/* ---- window bookkeeping ---------------------------------------------- */

typedef enum { HYG_INT, HYG_PREEMPT, HYG_IRQ } hyg_kind_t;

static void hyg_close(struct hygiene_cpu* c, hyg_kind_t kind,
                      uint64_t gross, uint64_t net,
                      const char* site, int line, uint32_t aux)
{
    struct hygiene_window* w;
    const char* what;
    uint64_t threshold;

    switch (kind) {
    case HYG_INT:
        w = &c->w_int;     what = "interrupts masked";   threshold = g_thr_int;
        break;
    case HYG_PREEMPT:
        w = &c->w_preempt; what = "preemption disabled"; threshold = g_thr_preempt;
        break;
    default:
        w = &c->w_irq;     what = "interrupt handler";   threshold = g_thr_irq;
        break;
    }

    w->total_count++;

    if (g_tsc_hz && net / (g_tsc_hz / 1000000ULL) > HYGIENE_IMPLAUSIBLE_US) {
        w->implausible_count++;
        return;
    }

    if (net > w->worst_net) {
        w->worst_net = net;
        w->worst_gross = gross;
        w->worst_site = site;
        w->worst_line = line;
        w->worst_aux = aux;
    }

    if (net < threshold) {
        return;
    }

    /*
     * Over. The gross check comes first because it is the cheap one; net is
     * what decides, so an interrupt storm during the window does not get
     * charged to the window. XNU re-checks the same way before it panics.
     */
    w->over_count++;

    if (c->busy) {
        return;
    }
    c->busy++;

    if (g_mode == HYGIENE_PANIC) {
        c->busy--;
        if (kind == HYG_IRQ) {
            panicf("hygiene: %s vector 0x%02x %lluus over %lluus on cpu%u",
                   what, (unsigned)aux,
                   (unsigned long long)hyg_us(net),
                   (unsigned long long)hyg_us(threshold),
                   (unsigned)percpu_index());
        }
        panicf("hygiene: %s %lluus over %lluus on cpu%u (%s:%d)",
               what,
               (unsigned long long)hyg_us(net),
               (unsigned long long)hyg_us(threshold),
               (unsigned)percpu_index(),
               site ? site : "?", line);
    }

    if (g_report_budget > 0) {
        g_report_budget--;
        kprintf("[hygiene] cpu%u %s %lluus (gross %lluus, limit %lluus)",
                (unsigned)percpu_index(), what,
                (unsigned long long)hyg_us(net),
                (unsigned long long)hyg_us(gross),
                (unsigned long long)hyg_us(threshold));
        /*
         * A masked window is named by where it was opened; an interrupt
         * handler by its vector. The preemption window has neither -- it is
         * opened by an inline in percpu.h, so the site would be that one line
         * for every caller. Naming it would be worse than leaving it blank.
         */
        if (kind == HYG_IRQ) {
            kprintf("  vector 0x%02x", (unsigned)aux);
        } else if (site) {
            kprintf("  at %s:%d", site, line);
        }
        kprintf("%s\n",
                (g_report_budget == 0) ? "  [further reports suppressed]" : "");
    }

    c->busy--;
}

void hygiene_int_begin(const char* site, int line)
{
    if (!g_hygiene_on) {
        return;
    }
    struct hygiene_cpu* c = hyg_cpu();
    /*
     * Deliberately not guarded by c->busy. The guard belongs on the reporting,
     * which re-enters through the console, and not on the bookkeeping, which
     * has to stay paired whatever else is happening -- an unpaired open is how
     * a window comes to span seconds.
     */
    if (c->int_start) {
        c->w_int.stale_count++;
    }
    c->int_site = site;
    c->int_line = line;
    c->int_start = hyg_now();
}

void hygiene_int_end(void)
{
    if (!g_hygiene_on) {
        return;
    }
    struct hygiene_cpu* c = hyg_cpu();
    if (!c->int_start) {
        return;
    }
    uint64_t gross = hyg_now() - c->int_start;
    c->int_start = 0;
    /*
     * Gross equals net here by construction: interrupts were masked for the
     * whole window, so no handler can have run inside it. The distinction is
     * kept in the signature anyway so the three windows read the same.
     */
    hyg_close(c, HYG_INT, gross, gross, c->int_site, c->int_line, 0);
}

void hygiene_preempt_begin(void)
{
    if (!g_hygiene_on) {
        return;
    }
    struct hygiene_cpu* c = hyg_cpu();
    if (c->preempt_start) {
        c->w_preempt.stale_count++;
    }
    c->preempt_irq_base = c->irq_ticks;
    c->preempt_start = hyg_now();
}

void hygiene_preempt_end(void)
{
    if (!g_hygiene_on) {
        return;
    }
    struct hygiene_cpu* c = hyg_cpu();
    if (!c->preempt_start) {
        return;
    }
    uint64_t gross = hyg_now() - c->preempt_start;
    uint64_t irq = c->irq_ticks - c->preempt_irq_base;
    c->preempt_start = 0;

    /* Interrupts stayed enabled through this window, so handlers did run in
     * it. They are not this window's doing and are not charged to it. */
    uint64_t net = (gross > irq) ? (gross - irq) : 0;
    hyg_close(c, HYG_PREEMPT, gross, net, NULL, 0, 0);
}

void hygiene_irq_enter(uint32_t vector)
{
    if (!g_hygiene_on) {
        return;
    }
    struct hygiene_cpu* c = hyg_cpu();
    /*
     * The open window is overwritten rather than nested.
     *
     * Nesting cannot happen through the gate -- it clears IF -- so an already
     * open window means the previous entry never came back: a thread that died
     * inside a handler, which scheduler_exit_current() does not return from.
     * A depth counter would be stuck at one from then on and quietly stop
     * measuring this processor for the rest of the boot. Overwriting loses one
     * sample and keeps measuring.
     */
    c->irq_vector = vector;
    c->irq_start = hyg_now();
}

void hygiene_irq_exit(uint32_t vector)
{
    (void)vector;
    if (!g_hygiene_on) {
        return;
    }
    struct hygiene_cpu* c = hyg_cpu();
    if (c->irq_start == 0) {
        return;
    }
    uint64_t gross = hyg_now() - c->irq_start;
    c->irq_start = 0;

    /* Added before the report, so a window closing on top of this one already
     * sees the handler time it needs to subtract. */
    c->irq_ticks += gross;

    hyg_close(c, HYG_IRQ, gross, gross, NULL, 0, c->irq_vector);
}

uint64_t hygiene_irq_ticks(void)
{
    if (!g_hygiene_on) {
        return 0;
    }
    return hyg_cpu()->irq_ticks;
}

/* ---- report ----------------------------------------------------------- */

static void hyg_line(const char* what, const struct hygiene_window* w,
                     uint64_t threshold, bool show_vector)
{
    if (w->total_count == 0) {
        return;
    }
    kprintf("[hygiene]   %-20s worst %lluus", what,
            (unsigned long long)hyg_us(w->worst_net));
    if (w->worst_gross != w->worst_net) {
        kprintf(" (gross %lluus)", (unsigned long long)hyg_us(w->worst_gross));
    }
    kprintf("  limit %lluus  over %llu of %llu",
            (unsigned long long)hyg_us(threshold),
            (unsigned long long)w->over_count,
            (unsigned long long)w->total_count);
    if (w->implausible_count) {
        kprintf("  discarded %llu implausible",
                (unsigned long long)w->implausible_count);
    }
    if (w->stale_count) {
        /* Loud rather than a footnote: any nonzero value here means the
         * numbers on this line are not to be trusted. */
        kprintf("  UNPAIRED %llu", (unsigned long long)w->stale_count);
    }
    if (show_vector) {
        kprintf("  worst vector 0x%02x", (unsigned)w->worst_aux);
    } else if (w->worst_site) {
        kprintf("  at %s:%d", w->worst_site, w->worst_line);
    }
    kprintf("\n");
}

void hygiene_report(void)
{
    if (g_mode == HYGIENE_OFF) {
        kprintf("[hygiene] off\n");
        return;
    }
    if (!g_hygiene_on) {
        kprintf("[hygiene] unavailable: no calibrated TSC\n");
        return;
    }

    kprintf("[hygiene] mode=%s tsc=%lluMHz\n",
            g_mode == HYGIENE_PANIC ? "panic" : "trace",
            (unsigned long long)(g_tsc_hz / 1000000ULL));

    for (uint32_t i = 0; i < HYGIENE_MAX_CPUS; i++) {
        struct hygiene_cpu* c = &g_hyg[i];
        if (c->w_int.total_count == 0 && c->w_preempt.total_count == 0 &&
            c->w_irq.total_count == 0) {
            continue;
        }
        kprintf("[hygiene] cpu%u\n", (unsigned)i);
        hyg_line("interrupts masked", &c->w_int, g_thr_int, false);
        hyg_line("preemption off", &c->w_preempt, g_thr_preempt, false);
        hyg_line("interrupt handler", &c->w_irq, g_thr_irq, true);
    }
}
