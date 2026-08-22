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

struct task;
struct thread;

/* The first four fields are reached from assembly by fixed offset. Keep them
 * at the head, and keep the offsets below in step with isr_stubs.S and
 * syscall_fast_entry.S; the static assertions further down fail the build if
 * they drift. */
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

    /* Last iretq frame the entry stubs were about to return through, captured
     * for the exception dump in isr_handlers.c. Per-CPU because a dump is
     * worth having precisely when several processors are faulting at once,
     * which is exactly when a shared slot would show another CPU's frame. */
    uint64_t irq_iret_rsp;
    uint64_t irq_iret_rip;
    uint64_t irq_iret_cs;
    uint64_t irq_iret_rflags;
    uint64_t isr_iret_rsp;
    uint64_t isr_iret_rip;
    uint64_t isr_iret_cs;
    uint64_t isr_iret_rflags;

    uint32_t index;              /* dense slot, assigned in bring-up order */
    uint32_t apic_id;            /* filled once the topology is known */

    struct task* current_task;
    struct thread* current_thread;

    /* This slot describes a processor that has actually been brought up.
     * Slots are zeroed .bss, so without this an IPI aimed at a CPU that was
     * never started would read apic_id 0 and hit the boot processor. */
    bool online;

    bool irq_checked;            /* percpu_irq_selftest() has run here */
};

/* Mirrored by the PCPU_* defines in isr_stubs.S and syscall_fast_entry.S. */
_Static_assert(offsetof(struct percpu, self) == 0,
               "percpu.self offset must match PCPU_SELF in the entry stubs");
_Static_assert(offsetof(struct percpu, tss_rsp0) == 8,
               "percpu.tss_rsp0 offset must match PCPU_TSS_RSP0 in the entry stubs");
_Static_assert(offsetof(struct percpu, syscall_user_rsp) == 16,
               "percpu.syscall_user_rsp offset must match PCPU_SYSCALL_USER_RSP in the entry stubs");
_Static_assert(offsetof(struct percpu, tls_fs_base) == 24,
               "percpu.tls_fs_base offset must match PCPU_TLS_FS_BASE in the entry stubs");
_Static_assert(offsetof(struct percpu, irq_iret_rsp) == 32,
               "percpu.irq_iret_rsp offset must match PCPU_IRQ_IRET_RSP in isr_stubs.S");
_Static_assert(offsetof(struct percpu, irq_iret_rip) == 40,
               "percpu.irq_iret_rip offset must match PCPU_IRQ_IRET_RIP in isr_stubs.S");
_Static_assert(offsetof(struct percpu, irq_iret_cs) == 48,
               "percpu.irq_iret_cs offset must match PCPU_IRQ_IRET_CS in isr_stubs.S");
_Static_assert(offsetof(struct percpu, irq_iret_rflags) == 56,
               "percpu.irq_iret_rflags offset must match PCPU_IRQ_IRET_RFLAGS in isr_stubs.S");
_Static_assert(offsetof(struct percpu, isr_iret_rsp) == 64,
               "percpu.isr_iret_rsp offset must match PCPU_ISR_IRET_RSP in isr_stubs.S");
_Static_assert(offsetof(struct percpu, isr_iret_rip) == 72,
               "percpu.isr_iret_rip offset must match PCPU_ISR_IRET_RIP in isr_stubs.S");
_Static_assert(offsetof(struct percpu, isr_iret_cs) == 80,
               "percpu.isr_iret_cs offset must match PCPU_ISR_IRET_CS in isr_stubs.S");
_Static_assert(offsetof(struct percpu, isr_iret_rflags) == 88,
               "percpu.isr_iret_rflags offset must match PCPU_ISR_IRET_RFLAGS in isr_stubs.S");

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
