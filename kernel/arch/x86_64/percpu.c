/**
 * @file percpu.c
 * @brief Per-CPU state addressed through the GS base.
 */

#include "percpu.h"
#include "../../../include/common.h"
#include "../../../include/debug.h"

#define IA32_GS_BASE        0xC0000101U
#define IA32_KERNEL_GS_BASE 0xC0000102U

struct percpu g_percpu[X86_64_MAX_CPUS];
bool g_percpu_gs_ready = false;

static inline void percpu_wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

void percpu_init_bsp(void)
{
    if (g_percpu_gs_ready) {
        return;
    }

    struct percpu* self = &g_percpu[0];
    memset(self, 0, sizeof(*self));
    self->self = self;
    self->index = 0;
    self->online = true;

    percpu_wrmsr(IA32_GS_BASE, (uint64_t)(uintptr_t)self);
    percpu_wrmsr(IA32_KERNEL_GS_BASE, (uint64_t)(uintptr_t)self);

    /* Publish only after both MSRs hold the base: percpu_self() switches on
     * this flag, and a reader that saw it early would dereference %gs:0
     * against a null base. */
    __asm__ volatile ("" ::: "memory");
    g_percpu_gs_ready = true;
}

void percpu_bind_bsp(uint32_t apic_id)
{
    percpu_self()->apic_id = apic_id;
}

void percpu_init_ap(uint32_t slot, uint32_t apic_id)
{
    if (slot >= X86_64_MAX_CPUS) {
        return;
    }

    struct percpu* self = &g_percpu[slot];
    memset(self, 0, sizeof(*self));
    self->self = self;
    self->index = slot;
    self->apic_id = apic_id;

    /* Runs before anything on this CPU may call percpu_self(): until these
     * two writes land, %gs on this processor still has a null base. */
    percpu_wrmsr(IA32_GS_BASE, (uint64_t)(uintptr_t)self);
    percpu_wrmsr(IA32_KERNEL_GS_BASE, (uint64_t)(uintptr_t)self);
}

const struct percpu* percpu_peer(uint32_t index)
{
    if (index >= X86_64_MAX_CPUS || !g_percpu[index].online) {
        return NULL;
    }
    return &g_percpu[index];
}

void percpu_irq_selftest(void)
{
    struct percpu* p = percpu_self();

    if (p->irq_checked) {
        return;
    }
    p->irq_checked = true;

    /* The self-pointer must survive the round trip through the GS base, and
     * the slot must be the array element its own index names. Either check
     * failing means the base is not this processor's slot. */
    if (p->self != p || p->index >= X86_64_MAX_CPUS || p != &g_percpu[p->index]) {
        panic("per-CPU state incoherent in interrupt context");
    }
}
