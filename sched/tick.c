#include "internal.h"
#include "../kernel/core/ktime.h"
#include "../kernel/arch/percpu.h"
#include "../kernel/core/cpu.h"
#include "../kernel/fabric/fabric.h"
#include "../kernel/unix/unix_layer.h"
#include "../kernel/usb/usb.h"
#include "../include/console.h"
#include "../include/gfx_console.h"

/* Actual timer frequency set by scheduler_set_tick_rate(); used to derive
 * dividers that fire at a fixed real-time rate regardless of tick frequency. */
static uint32_t g_tick_hz = 100u;

/* Heartbeat interval in nanoseconds; zero disables it. Set from
 * rdnx.heartbeat=<seconds>. */
uint64_t g_heartbeat_ns = 0;

/* Target rates for periodic subsystems (Hz). */
#define CONSOLE_TICK_HZ   100u   /* cursor blink driver */
#define USB_CLASS_POLL_HZ  50u   /* HID/MSC class poll  (~20 ms) */
#define USB_HOST_POLL_HZ    1u   /* host safety fallback (~1 s)  */

/*
 * Say, periodically, what this processor is doing.
 *
 * The one fact a hang does not give up on its own is whether the machine is
 * still running. Everything else follows from it: if this keeps printing while
 * nothing progresses, threads are alive and waiting for a wakeup that never
 * comes; if it stops, something is wedged and the question is what.
 *
 * Each processor reports only its own per-CPU state. That restriction is the
 * lesson from the last attempt at this, where a tracer read the run queues
 * without their lock and pointed at the wrong subsystem for several rounds.
 */
static void scheduler_heartbeat(void)
{
    if (g_heartbeat_ns == 0) {
        return;
    }
    struct percpu* p = percpu_self();
    uint64_t now = ktime_ns();
    if (now - p->hb_last_ns < g_heartbeat_ns) {
        return;
    }
    p->hb_last_ns = now;

    extern uint32_t ready_queue_len_locked(void);
    extern uint32_t ready_queue_snapshot_locked(uint64_t*, int*, uint32_t*, uint32_t);
    thread_t* cur = p->current_thread;
    uint64_t ids[4];
    int sts[4];
    uint32_t bks[4];
    uint32_t nq = ready_queue_snapshot_locked(ids, sts, bks, 4);
    kprintf("[HB] cpu%u t=%llums tid=%llu state=%d idle=%d preempt=%u "
            "resched=%d insw=%d slice=%u ready=%u ticks=%llu\n",
            (unsigned)p->index,
            (unsigned long long)(now / 1000000ULL),
            (unsigned long long)(cur ? cur->thread_id : 0),
            (int)(cur ? (int)cur->state : -1),
            (cur && cur == p->sched_idle) ? 1 : 0,
            (unsigned)p->preempt_count,
            p->sched_resched_pending ? 1 : 0,
            p->sched_in_switch ? 1 : 0,
            (unsigned)p->sched_ticks_until_preempt,
            (unsigned)ready_queue_len_locked(),
            (unsigned long long)p->hb_ticks);
    kprintf("[HB]   switches=%llu queued:",
            (unsigned long long)stats.total_switches);
    for (uint32_t i = 0; i < nq; i++) {
        kprintf(" tid=%llu/state=%d/bucket=%u",
                (unsigned long long)ids[i], sts[i], (unsigned)bks[i]);
    }
    kprintf("\n");

    if (p->index == 0) {
        extern uint32_t task_debug_thread_snapshot(uint64_t*, uint64_t*, int*,
                                                   uint64_t*, uint64_t*, uint32_t);
        uint64_t tids[24], tsks[24], wqs[24], rips[24];
        int sts[24];
        uint32_t n = task_debug_thread_snapshot(tids, tsks, sts, wqs, rips, 24);
        for (uint32_t i = 0; i < n; i++) {
            kprintf("[TH] tid=%llu task=%llu state=%d waitq=%s rip=%llx\n",
                    (unsigned long long)tids[i],
                    (unsigned long long)tsks[i],
                    sts[i],
                    wqs[i] ? "yes" : "NONE",
                    (unsigned long long)rips[i]);
        }
    }
}

void scheduler_tick(void)
{
    static uint32_t usb_class_divider = 0;
    static uint32_t usb_poll_divider  = 0;
    static uint32_t console_divider   = 0;

    percpu_self()->hb_ticks++;
    scheduler_heartbeat();

    if (!scheduler_running) {
        return;
    }

    /* Global time is advanced by one processor only. Every CPU takes this
     * interrupt, so incrementing here from all of them would make the clock
     * run N times fast and fire every timeout N times early. The same goes
     * for the periodic subsystem polls further down. */
    const bool owns_global_time = (cpu_get_id() == 0);
    if (owns_global_time) {
        sched_ticks++;
        waitq_tick(sched_ticks);
    }

    thread_t* cur = thread_get_current();
    if (cur && thread_is_runnable(cur)) {
        cur->sched_usage = (cur->sched_usage * 7) / 8;
        cur->sched_usage++;
        /* Обновить CPU-счётчики группы (task_t.thread_group) */
        if (cur->task) {
            cur->task->thread_group.cpu_ticks++;
            cur->task->thread_group.last_run_tick = sched_ticks;
        }
        if (cur->sched_class == SCHED_CLASS_TIMESHARE) {
            if ((cur->sched_usage % PENALTY_STEP_TICKS) == 0) {
                int base = cur->base_priority;
                int dyn = cur->dyn_priority - 1;
                cur->dyn_priority = clamp_dyn_priority(dyn, base);
            }
        }
    }

    if (ticks_until_preempt > 0) {
        ticks_until_preempt--;
    }

    if (ticks_until_preempt == 0) {
        ticks_until_preempt = ticks_per_slice;
        resched_pending = true;
    }

    if (!owns_global_time) {
        /* Preemption accounting above is per-CPU and has already happened.
         * Everything below drives machine-wide subsystems. */
        return;
    }

    (void)fabric_dispatcher_tick();

#define DIVIDER_FIRE(counter, target_hz, body)          \
    do {                                                 \
        uint32_t _p = g_tick_hz / (target_hz);          \
        if (_p < 1u) { _p = 1u; }                       \
        if (++(counter) >= _p) { (counter) = 0; body }  \
    } while (0)

    DIVIDER_FIRE(usb_class_divider, USB_CLASS_POLL_HZ, usb_class_poll_all(););
    DIVIDER_FIRE(usb_poll_divider,  USB_HOST_POLL_HZ,  usb_host_poll_all(););
    DIVIDER_FIRE(console_divider,   CONSOLE_TICK_HZ,   console_tick(););
}

void scheduler_set_tick_rate(uint32_t hz)
{
    if (hz == 0) {
        return;
    }

    g_tick_hz = hz;
    gfx_console_set_tick_hz(CONSOLE_TICK_HZ);

    uint32_t ticks = (hz * SCHEDULER_TIME_SLICE_MS + 999) / 1000;
    if (ticks == 0) {
        ticks = 1;
    }
    ticks_per_slice = ticks;
    if (ticks_until_preempt > ticks_per_slice) {
        ticks_until_preempt = ticks_per_slice;
    }
}

void scheduler_ast_check(void)
{
    percpu_self()->hb_ticks++;
    scheduler_heartbeat();

    if (!scheduler_running) {
        return;
    }

    /* Deliver pending Unix signals in long-running kernel-side loops such as
     * tty/input reads, where userspace may otherwise never reach a syscall
     * return boundary. */
    unix_proc_signal_checkpoint();
}
