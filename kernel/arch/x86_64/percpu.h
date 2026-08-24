/**
 * @file percpu.h
 * @brief Per-CPU state addressed through the GS base.
 *
 * Each processor owns one `struct percpu`. The processor's own slot is
 * reached through its GS base rather than through an array index, so a read
 * costs one segment-relative load and is safe from interrupt context: there
 * is no lock, no global to consult, and nothing that can be preempted
 * halfway.
 *
 * GS ownership (important, and temporary):
 *   Userspace cannot set a GS base on this system. arch_prctl implements
 *   only ARCH_SET_FS / ARCH_GET_FS (kernel/linux/linux_compat.c), and TLS
 *   runs on FS. GS therefore belongs to the kernel at all times, which is
 *   why kernel code may read %gs: without a preceding swapgs today.
 *
 *   IA32_GS_BASE and IA32_KERNEL_GS_BASE are both pointed at the same slot
 *   for the same reason: with nothing to swap, either half answers
 *   correctly, and the entry paths can adopt swapgs incrementally without a
 *   flag day.
 *
 *   The moment userspace can set a GS base -- an ARCH_SET_GS, a 32-bit
 *   personality, anything -- reading %gs: in kernel code without swapgs
 *   becomes a user-controlled pointer dereference. Adding that syscall and
 *   adding swapgs to the entry stubs is one change, not two.
 */

#ifndef _RODNIX_ARCH_X86_64_PERCPU_H
#define _RODNIX_ARCH_X86_64_PERCPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu_topology.h"
#include "../../core/hygiene.h"

struct task;
struct thread;

/* The first four fields are reached from assembly by fixed offset. Keep them
 * at the head, and keep the offsets below in step with isr_stubs.S and
 * syscall_fast_entry.S; the static assertions further down fail the build if
 * they drift. */
/* Cache-line aligned so two processors' slots never share a line. XNU marks
 * its hot per-CPU fields the same way; without it, one CPU touching its own
 * state invalidates a neighbour's. */
struct percpu {
    /* Offset 0 by contract: `mov %gs:0, %reg` yields the base itself, which
     * is how assembly reaches this struct without a relocation. */
    struct percpu* self;

    /* Kernel stack pointer this CPU switches to on SYSCALL entry. Mirrors
     * tss.rsp0, which the CPU itself only consults for ring transitions
     * through an interrupt gate -- SYSCALL does no stack switch of its own. */
    uint64_t tss_rsp0;

    /* Where the SYSCALL entry parks the user stack pointer between the
     * instruction that clobbers RSP and the trapframe store. Per-CPU because
     * two processors in a syscall at once would otherwise overwrite each
     * other's user RSP. */
    uint64_t syscall_user_rsp;

    /* FS.Base of the thread running on this CPU. Loading a segment selector
     * in 64-bit mode zeroes the hidden base, so the entry paths re-apply it
     * from here on the way back out. */
    uint64_t tls_fs_base;

    /* Last iretq frame the entry stub was about to return through, captured
     * for the exception dump in isr_handlers.c. Per-CPU because a dump is
     * worth having precisely when several processors are faulting at once,
     * which is exactly when a shared slot would show another CPU's frame.
     * One set, because there is one stub. */
    uint64_t iret_rsp;
    uint64_t iret_rip;
    uint64_t iret_cs;
    uint64_t iret_rflags;

    /* Address of the on_cpu word belonging to the thread this processor has
     * just switched away from, or zero. The interrupt stub clears it right
     * after loading the new stack pointer -- the first instant at which the
     * old thread's stack is provably no longer in use. An address rather than
     * a thread pointer so the assembly needs no knowledge of thread_t.
     *
     * Read by isr_stubs.S at PCPU_PREV_ONCPU. */
    volatile uint32_t* sched_prev_oncpu;

    /* Physical address of the page tables this processor is running on.
     *
     * Per-CPU because CR3 is: a global mirror of it reports whatever the last
     * processor to switch happened to be running, so a fault handler would
     * look up "the current page tables" and be handed another process's.
     *
     * A field rather than a CR3 read, following both references -- FreeBSD's
     * pc_curpmap and XNU's cpu_active_cr3. Reading a control register costs
     * tens of cycles and this is consulted on every page mapped. */
    uint64_t current_pml4;

    /* Interrupt request level of this processor. Was a single global, which
     * on SMP means every CPU reporting whatever the last one set. */
    uint32_t irql;

    /* Scheduler state that describes *this* processor rather than the
     * system: whether it is inside a switch, whether it owes itself a
     * reschedule, and how much of its current thread's slice is left. All
     * three were globals, which on SMP means one processor's decision
     * silently becoming every processor's. */

    /* This processor's idle thread: always runnable, never in the shared run
     * queue, so "nothing to run" is a thread rather than a state the switch
     * path has to invent an answer for. */
    struct thread* sched_idle;

    /* Nonzero while this processor must not be switched away from.
     *
     * A spinlock held with interrupts enabled is not enough in a preemptive
     * kernel: the holder can be preempted, and the next thread on the same
     * processor spins for a lock whose owner will never be scheduled again.
     * Raising this makes the timer defer the switch instead, which costs a
     * tick of latency rather than the machine. */
    uint32_t preempt_count;

    bool sched_in_switch;
    bool sched_resched_pending;
    uint32_t sched_ticks_until_preempt;

    uint32_t index;              /* dense slot, assigned in bring-up order */
    uint32_t apic_id;            /* filled once the topology is known */

    struct task* current_task;
    struct thread* current_thread;

    /* This slot describes a processor that has actually been brought up.
     * Slots are zeroed .bss, so without this an IPI aimed at a CPU that was
     * never started would read apic_id 0 and hit the boot processor. */
    /* Console recursion guard. Was a global, which on SMP makes one
     * processor's ordinary print look like another's re-entry. */
    bool console_in_progress;

    bool online;

    bool irq_checked;            /* percpu_irq_selftest() has run here */

    /* Absolute TSC value this processor's timer is next due to fire at.
     * Per-CPU because each processor arms its own LAPIC timer, and absolute
     * because a rate is only kept by scheduling against the previous deadline
     * rather than against whenever the handler happened to run. */
    uint64_t timer_deadline;
    uint64_t timer_missed;       /* periods skipped after falling behind */

    /* Heartbeat bookkeeping: when this processor last reported, and how many
     * timer interrupts it has taken. Per-CPU so the report needs no lock. */
    uint64_t hb_last_ns;
    uint64_t hb_ticks;
} __attribute__((aligned(64)));

/* Mirrored by the PCPU_* defines in isr_stubs.S and syscall_fast_entry.S. */
_Static_assert(offsetof(struct percpu, self) == 0,
               "percpu.self offset must match PCPU_SELF in the entry stubs");
_Static_assert(offsetof(struct percpu, tss_rsp0) == 8,
               "percpu.tss_rsp0 offset must match PCPU_TSS_RSP0 in the entry stubs");
_Static_assert(offsetof(struct percpu, syscall_user_rsp) == 16,
               "percpu.syscall_user_rsp offset must match PCPU_SYSCALL_USER_RSP in the entry stubs");
_Static_assert(offsetof(struct percpu, tls_fs_base) == 24,
               "percpu.tls_fs_base offset must match PCPU_TLS_FS_BASE in the entry stubs");
_Static_assert(offsetof(struct percpu, iret_rsp) == 32,
               "percpu.iret_rsp offset must match PCPU_IRET_RSP in isr_stubs.S");
_Static_assert(offsetof(struct percpu, iret_rip) == 40,
               "percpu.iret_rip offset must match PCPU_IRET_RIP in isr_stubs.S");
_Static_assert(offsetof(struct percpu, iret_cs) == 48,
               "percpu.iret_cs offset must match PCPU_IRET_CS in isr_stubs.S");
_Static_assert(offsetof(struct percpu, iret_rflags) == 56,
               "percpu.iret_rflags offset must match PCPU_IRET_RFLAGS in isr_stubs.S");
_Static_assert(offsetof(struct percpu, sched_prev_oncpu) == 64,
               "percpu.sched_prev_oncpu offset must match PCPU_PREV_ONCPU in isr_stubs.S");

extern struct percpu g_percpu[X86_64_MAX_CPUS];
extern bool g_percpu_gs_ready;

/* Point this processor's GS base at its slot. The BSP takes slot 0.
 * Bring-up order rather than MADT order deliberately: tying the slot index
 * to the firmware table would make per-CPU state depend on ACPI parsing,
 * and per-CPU state has to work before ACPI is read. */
void percpu_init_bsp(void);

/* Claim `slot` for the processor executing this and point its GS base at it.
 * The AP counterpart of percpu_init_bsp(); the slot is chosen by the boot
 * processor before the STARTUP IPI goes out. Does not mark the slot online --
 * that is the last thing an AP does, once it is genuinely usable. */
void percpu_init_ap(uint32_t slot, uint32_t apic_id);

/* Record the APIC ID once cpu_topology has determined it. */
void percpu_bind_bsp(uint32_t apic_id);

/* Slot for another processor, or NULL if that index is not online. Only for
 * things that legitimately address another CPU, such as an IPI destination. */
const struct percpu* percpu_peer(uint32_t index);

/* Verify, from interrupt context, that %gs resolves to this processor's own
 * coherent slot. Runs once per processor off the first timer interrupt, so
 * every AP brought up later is checked the same way without extra wiring.
 * Panics on mismatch: a wrong GS base means every per-CPU read taken in an
 * interrupt -- current thread, current task, the fault path -- is answering
 * from another processor's state or from unmapped memory, and continuing
 * would corrupt silently instead of stopping. */
void percpu_irq_selftest(void);

static inline struct percpu* percpu_self(void)
{
    if (!g_percpu_gs_ready) {
        /* Before the GS base is loaded only the BSP is running, and it owns
         * slot 0 -- the very object GS will point at. Reads and writes in
         * this window land in the same memory as afterwards, so no state is
         * stranded by the switch. */
        return &g_percpu[0];
    }

    struct percpu* p;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(p));
    return p;
}

static inline uint32_t percpu_index(void)
{
    return percpu_self()->index;
}

static inline uint32_t percpu_apic_id(void)
{
    return percpu_self()->apic_id;
}

/* Preemption control. Nesting counts, so a lock taken inside another lock
 * does not re-enable switching when the inner one is dropped.
 *
 * The outermost transition -- 0 to 1 and back -- is also the boundary of a
 * latency window: for as long as the count is nonzero this processor will not
 * switch away, whoever is waiting. Measuring it here rather than at each of a
 * hundred lock sites is the whole reason the count exists in one place.
 * hygiene_enabled() is a plain global read so the cost when it is off is a
 * predictable branch. */
static inline void percpu_preempt_disable_at(const char* file, int line)
{
    struct percpu* p = percpu_self();
    if (p->preempt_count++ == 0 && hygiene_enabled()) {
        hygiene_preempt_begin(file, line);
    }
    __asm__ volatile ("" ::: "memory");
}

static inline void percpu_preempt_disable(void)
{
    percpu_preempt_disable_at(NULL, 0);
}

static inline void percpu_preempt_enable(void)
{
    __asm__ volatile ("" ::: "memory");
    struct percpu* p = percpu_self();
    if (p->preempt_count > 0) {
        if (--p->preempt_count == 0 && hygiene_enabled()) {
            /* After the count has reached zero, so the reporting path may take
             * locks of its own without re-entering this window. */
            hygiene_preempt_end();
        }
    }
}

static inline bool percpu_preempt_blocked(void)
{
    return percpu_self()->preempt_count != 0;
}

/* FS.Base the entry stubs should restore on the way back to userspace. */
static inline void percpu_set_tls_fs_base(uint64_t base)
{
    percpu_self()->tls_fs_base = base;
}

static inline uint64_t percpu_get_tls_fs_base(void)
{
    return percpu_self()->tls_fs_base;
}

/* Kernel stack the SYSCALL entry switches to. Kept in step with tss.rsp0 by
 * tss_set_rsp0(). */
static inline void percpu_set_tss_rsp0(uint64_t rsp0)
{
    percpu_self()->tss_rsp0 = rsp0;
}

#endif /* _RODNIX_ARCH_X86_64_PERCPU_H */
