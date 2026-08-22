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
#include <stdint.h>

#include "cpu_topology.h"

struct task;
struct thread;

struct percpu {
    /* Offset 0 by contract: `mov %gs:0, %reg` yields the base itself, which
     * is how assembly reaches this struct without a relocation. */
    struct percpu* self;

    uint32_t index;              /* dense slot, assigned in bring-up order */
    uint32_t apic_id;            /* filled once the topology is known */

    struct task* current_task;
    struct thread* current_thread;

    bool irq_checked;            /* percpu_irq_selftest() has run here */
};

extern struct percpu g_percpu[X86_64_MAX_CPUS];
extern bool g_percpu_gs_ready;

/* Point this processor's GS base at its slot. The BSP takes slot 0.
 * Bring-up order rather than MADT order deliberately: tying the slot index
 * to the firmware table would make per-CPU state depend on ACPI parsing,
 * and per-CPU state has to work before ACPI is read. */
void percpu_init_bsp(void);

/* Record the APIC ID once cpu_topology has determined it. */
void percpu_bind_bsp(uint32_t apic_id);

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

#endif /* _RODNIX_ARCH_X86_64_PERCPU_H */
