/**
 * @file gdt.c
 * @brief Minimal GDT/TSS setup for ring3 support
 */

#include "gdt.h"
#include "percpu.h"
#include "idt.h"
#include <stdint.h>
#include "../../include/common.h"
#include "../../../include/console.h"
#include "../../../lib/heap.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) gdt_tss_entry_t;

typedef struct {
    gdt_entry_t null;
    gdt_entry_t kcode;
    gdt_entry_t kdata;
    gdt_entry_t udata;
    gdt_entry_t ucode;
    gdt_tss_entry_t tss;
} __attribute__((packed)) gdt_table_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss64_t;

/* One GDT and one TSS per processor. The layout is identical on every CPU,
 * so GDT_TSS_SEL stays a compile-time constant and only the TSS base inside
 * the descriptor differs -- which is the whole point: rsp0 and the IST stacks
 * are per-processor, and a shared TSS would hand two CPUs the same kernel
 * stack on the next ring transition.
 *
 * Static rather than heap-allocated because gdt_init() runs inside cpu_init(),
 * the first sysinit step, long before heap_init(). At roughly 176 bytes per
 * processor the whole array is a few kilobytes of .bss. */
static gdt_table_t g_gdt[X86_64_MAX_CPUS];
static tss64_t g_tss[X86_64_MAX_CPUS];

/* IST stacks, by contrast, are allocated later (see cpu_ist_init): sizing
 * three stacks for every possible processor up front would cost far more
 * than the descriptors do. */
#define IST_STACK_SIZE 8192

/* Stubs from isr_stubs.S, re-pointed at an IST index by cpu_ist_init(). */
extern void isr2(void);
extern void isr8(void);
extern void isr18(void);

/* TSS ist1..ist7 are 1-based; these are the indices written into IDT gates. */
#define IST_DOUBLE_FAULT  1
#define IST_NMI           2
#define IST_MACHINE_CHECK 3

static void gdt_set_entry(gdt_entry_t* e, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    e->limit_low = (uint16_t)(limit & 0xFFFF);
    e->base_low = (uint16_t)(base & 0xFFFF);
    e->base_mid = (uint8_t)((base >> 16) & 0xFF);
    e->access = access;
    e->gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    e->base_high = (uint8_t)((base >> 24) & 0xFF);
}

static void gdt_set_tss(gdt_tss_entry_t* e, uint64_t base, uint32_t limit)
{
    e->limit_low = (uint16_t)(limit & 0xFFFF);
    e->base_low = (uint16_t)(base & 0xFFFF);
    e->base_mid = (uint8_t)((base >> 16) & 0xFF);
    e->access = 0x89; /* present, type 9 (available TSS) */
    e->gran = (uint8_t)(((limit >> 16) & 0x0F));
    e->base_high = (uint8_t)((base >> 24) & 0xFF);
    e->base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    e->reserved = 0;
}

void gdt_init(void)
{
    /* Which slot is decided by asking the per-CPU state, which is why
     * percpu_init runs before cpu_init in the sysinit order. */
    uint32_t cpu = percpu_index();
    if (cpu >= X86_64_MAX_CPUS) {
        cpu = 0;
    }

    gdt_table_t* gdt = &g_gdt[cpu];
    tss64_t* tss = &g_tss[cpu];

    memset(gdt, 0, sizeof(*gdt));
    memset(tss, 0, sizeof(*tss));

    gdt_set_entry(&gdt->kcode, 0, 0, 0x9A, 0xA0);
    gdt_set_entry(&gdt->kdata, 0, 0, 0x92, 0xC0);
    gdt_set_entry(&gdt->udata, 0, 0, 0xF2, 0xC0);
    gdt_set_entry(&gdt->ucode, 0, 0, 0xFA, 0xA0);

    tss->iomap_base = (uint16_t)sizeof(*tss);
    gdt_set_tss(&gdt->tss, (uint64_t)(uintptr_t)tss, sizeof(*tss) - 1);

    gdt_ptr_t gdt_ptr;
    gdt_ptr.limit = (uint16_t)(sizeof(*gdt) - 1);
    gdt_ptr.base = (uint64_t)(uintptr_t)gdt;

    __asm__ volatile ("lgdt %0" : : "m"(gdt_ptr));
    __asm__ volatile (
        "movw %0, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        :
        : "r"((uint16_t)GDT_KERNEL_DS)
        : "memory", "rax"
    );

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_TSS_SEL));
}

void tss_set_rsp0(uint64_t rsp0)
{
    uint32_t cpu = percpu_index();
    if (cpu >= X86_64_MAX_CPUS) {
        cpu = 0;
    }
    g_tss[cpu].rsp0 = rsp0;
    /* SYSCALL performs no stack switch of its own, so the entry stub reads
     * the same value out of this CPU's percpu slot instead of the TSS. */
    percpu_set_tss_rsp0(rsp0);
}

/* The TSS is packed and its 64-bit fields sit at odd offsets, so the IST
 * slots are written by index rather than through a pointer -- taking the
 * address of a packed member is undefined behaviour even where the hardware
 * would not care. */
static void tss_set_ist(tss64_t* tss, uint8_t index, uint64_t top)
{
    switch (index) {
    case 1: tss->ist1 = top; break;
    case 2: tss->ist2 = top; break;
    case 3: tss->ist3 = top; break;
    default: break;
    }
}

/* Give the fatal exceptions their own stacks.
 *
 * Separate from gdt_init() because it needs the heap, and gdt_init() runs
 * before there is one. Until this has run every gate uses IST 0, which is
 * the behaviour the tree had all along.
 *
 * The point is #DF: today a kernel stack overflow cannot push an exception
 * frame, so it escalates straight to a triple fault and the machine resets
 * with nothing on the wire. With IST the CPU switches to a known-good stack
 * and the handler gets to say what happened. NMI and #MC get the same
 * treatment because they can arrive on any stack at any time.
 *
 * Must run with interrupts still masked: it rewrites live IDT gates, and a
 * gate naming an IST index whose TSS slot is still zero would load RSP = 0. */
int cpu_ist_init(void)
{
    uint32_t cpu = percpu_index();
    if (cpu >= X86_64_MAX_CPUS) {
        return -1;
    }

    tss64_t* tss = &g_tss[cpu];

    struct {
        uint8_t index;
        uint16_t vector;
        void (*handler)(void);
        const char* name;
    } ist_map[] = {
        { IST_DOUBLE_FAULT,  8,  isr8,  "#DF" },
        { IST_NMI,           2,  isr2,  "NMI" },
        { IST_MACHINE_CHECK, 18, isr18, "#MC" },
    };

    for (unsigned i = 0; i < sizeof(ist_map) / sizeof(ist_map[0]); i++) {
        void* stack = kmalloc(IST_STACK_SIZE);
        if (!stack) {
            kprintf("[IST] cpu%u: allocation failed for %s, leaving it on IST 0\n",
                    (unsigned)cpu, ist_map[i].name);
            continue;
        }

        /* Stacks grow down, and the CPU wants the top. Keep it 16-byte
         * aligned so the handler's first push lands on an aligned frame. */
        uint64_t top = ((uint64_t)(uintptr_t)stack + IST_STACK_SIZE) & ~0xFULL;
        tss_set_ist(tss, ist_map[i].index, top);

        /* Only now is the gate allowed to name this index. */
        idt_set_handler(ist_map[i].vector,
                        (void*)ist_map[i].handler,
                        IDT_TYPE_INTERRUPT_GATE,
                        ist_map[i].index);
    }

    return 0;
}
