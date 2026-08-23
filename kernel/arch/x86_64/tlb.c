/**
 * @file tlb.c
 * @brief Cross-processor TLB invalidation.
 */

#include "tlb.h"
#include "cpu_topology.h"
#include "percpu.h"
#include "vectors.h"
#include "../../core/interrupts.h"
#include "../../fabric/spin.h"
#include "../../../include/console.h"
#include <stdbool.h>

/* One shootdown at a time. Serialising them keeps the published request and
 * the scoreboard to a single set instead of one per processor; with the
 * processor counts this runs on, the contention that buys back is not worth
 * the extra state. FreeBSD publishes per-CPU because it scales further. */
static spinlock_t tlb_lock;

static volatile uint64_t tlb_target_va;
static volatile uint32_t tlb_generation;
static volatile uint32_t tlb_ack[X86_64_MAX_CPUS];

static inline void tlb_flush_local(uint64_t va)
{
    if (va) {
        __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
    } else {
        uint64_t cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
    }
}

static void tlb_shootdown_handler(interrupt_context_t* ctx)
{
    (void)ctx;

    uint64_t va = tlb_target_va;
    uint32_t gen = tlb_generation;

    tlb_flush_local(va);

    /* Acknowledge only after the flush has happened, and with a release so
     * the initiator cannot observe the acknowledgement before its effect. */
    uint32_t self = percpu_index();
    if (self < X86_64_MAX_CPUS) {
        __atomic_store_n(&tlb_ack[self], gen, __ATOMIC_RELEASE);
    }
}

void tlb_shootdown_init(void)
{
    spinlock_init(&tlb_lock);
    (void)interrupt_register(VECTOR_TLB_SHOOTDOWN, tlb_shootdown_handler);
}

void tlb_shootdown(uint64_t va)
{
    /* This processor first: it needs no message and no acknowledgement. */
    tlb_flush_local(va);

    uint32_t total = cpu_topology_count();
    if (total <= 1) {
        return;
    }

    uint32_t self = percpu_index();
    uint64_t flags = spinlock_lock_irqsave(&tlb_lock);

    tlb_target_va = va;
    uint32_t gen = ++tlb_generation;
    if (gen == 0) {
        gen = ++tlb_generation;   /* zero means "never acknowledged" */
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);

    uint32_t sent = 0;
    for (uint32_t cpu = 0; cpu < total && cpu < X86_64_MAX_CPUS; cpu++) {
        if (cpu == self || percpu_peer(cpu) == NULL) {
            continue;
        }
        if (interrupt_send_ipi(cpu, VECTOR_TLB_SHOOTDOWN) == 0) {
            sent++;
        }
    }

    if (sent > 0) {
        /*
         * Wait for every target. Bounded, because a processor that never
         * answers has failed in a way that silence would not explain, and a
         * stale translation left behind is worse than a loud stop.
         */
        for (uint32_t cpu = 0; cpu < total && cpu < X86_64_MAX_CPUS; cpu++) {
            if (cpu == self || percpu_peer(cpu) == NULL) {
                continue;
            }
            uint64_t spins = 0;
            while (__atomic_load_n(&tlb_ack[cpu], __ATOMIC_ACQUIRE) != gen) {
                __asm__ volatile ("pause");
                if (++spins == 500000000ULL) {
                    kprintf("[TLB] cpu%u never acknowledged shootdown gen=%u\n",
                            (unsigned)cpu, (unsigned)gen);
                    break;
                }
            }
        }
    }

    spinlock_unlock_irqrestore(&tlb_lock, flags);
}
