/**
 * @file smp.c
 * @brief Application processor bring-up.
 */

#include "smp.h"
#include "apic.h"
#include "config.h"
#include "cpu_topology.h"
#include "gdt.h"
#include "idt.h"
#include "lapic_access.h"
#include "paging.h"
#include "percpu.h"
#include "../../core/cpu.h"
#include "../../core/interrupts.h"
#include "pmm.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../lib/heap.h"

/* Symbols inside the trampoline blob. Their addresses here are wherever the
 * linker put the blob; what matters is the offset of each from the start,
 * which is what gets applied to the copy in low memory. */
extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];
extern uint8_t ap_trampoline_cr3[];
extern uint8_t ap_trampoline_stack[];
extern uint8_t ap_trampoline_entry[];

#define AP_STACK_SIZE      (16 * 1024)
#define AP_ONLINE_TIMEOUT_MS 200u
#define SIPI_GAP_US        200u

/* Which slot the processor currently being started should claim. Bring-up is
 * serialised -- one AP at a time -- precisely so this, and the single stack
 * slot in the trampoline, can be plain variables. */
static volatile uint32_t g_ap_boot_slot = 0;
static volatile uint32_t g_ap_boot_apic_id = 0;
static uint64_t g_kernel_pml4_phys = 0;
static uint32_t g_online_count = 1; /* the boot processor */

static uint64_t read_cr3(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFFULL;
}

static void* tramp_ptr(uint8_t* blob_symbol)
{
    uintptr_t offset = (uintptr_t)blob_symbol - (uintptr_t)ap_trampoline_start;
    return (void*)((uintptr_t)X86_64_PHYS_TO_VIRT(AP_TRAMPOLINE_PHYS) + offset);
}

static void tramp_patch(uint8_t* blob_symbol, uint64_t value)
{
    *(volatile uint64_t*)tramp_ptr(blob_symbol) = value;
}

/*
 * Page tables for the window between the STARTUP IPI and the jump to the
 * kernel's own address space.
 *
 * The AP needs low memory identity-mapped, because the trampoline executes at
 * physical addresses right up to the moment paging comes on. The kernel's
 * live tables cannot serve: paging_disable_identity_map() cleared pml4[0]
 * during boot, and restoring it there would punch a hole in the isolation of
 * every process for the whole bring-up window.
 *
 * So a private PML4 instead. The identity half reuses the PDPT that boot.S
 * already built for the first gigabyte -- it is still populated, only the
 * pml4 entry pointing at it was removed -- and the kernel half is copied
 * wholesale so the AP's stack and code are addressable the instant it lands
 * in 64-bit mode.
 */
static uint64_t build_trampoline_pml4(void)
{
    extern uint64_t pdpt0[];

    /* CR3 is loaded while the processor is still in 32-bit protected mode, so
     * the table must live below 4GB. The LOW zone caps at 16MB. */
    uint64_t phys = pmm_alloc_page_in_zone(PMM_ZONE_LOW);
    if (!phys) {
        return 0;
    }

    uint64_t* tramp = (uint64_t*)X86_64_PHYS_TO_VIRT(phys);
    memset(tramp, 0, 4096);

    /* boot.S links its early tables at their physical addresses. */
    tramp[0] = ((uint64_t)(uintptr_t)pdpt0) | PTE_PRESENT | PTE_RW;

    const uint64_t* kernel = (const uint64_t*)X86_64_PHYS_TO_VIRT(g_kernel_pml4_phys);
    for (uint32_t i = 256; i < 512; i++) {
        tramp[i] = kernel[i];
    }

    return phys;
}

/* Entered from the trampoline with a stack, the kernel's high half mapped and
 * nothing else set up. Runs once per application processor. */
void ap_entry(void);
void ap_entry(void)
{
    uint32_t slot = g_ap_boot_slot;
    uint32_t apic_id = g_ap_boot_apic_id;

    /* First, because everything below reaches per-CPU state through %gs. */
    percpu_init_ap(slot, apic_id);

    /* Leave the trampoline's tables. Safe here and not earlier: this code is
     * already executing in the high half, which the kernel PML4 maps. */
    paging_switch_pml4(g_kernel_pml4_phys);

    gdt_init();     /* claims this CPU's GDT and TSS by percpu index */
    idt_load();     /* shared table, per-CPU register */
    cpu_ist_init(); /* this CPU's own #DF/NMI/#MC stacks */
    apic_init_ap();

    /*
     * This processor's LAPIC timer is deliberately left stopped.
     *
     * Starting it is one line -- the calibration is machine-wide and
     * apic_timer_start() touches only per-CPU registers -- and it does put
     * the AP into the scheduler off the shared run queue. It was tried and
     * backed out: the switch path has no thread to fall back on when an AP's
     * thread exits and the queue is empty, so it resumes a dead thread and
     * takes a #GP on the next iretq. Fixing that properly means giving each
     * processor its own idle thread, and handing a preempted thread over
     * with an on-cpu handshake rather than the tick-deferred approximation
     * in sched/switch.c. Both are written up in docs/ru/smp_bringup.md.
     *
     * Until then this processor stays reachable by IPI and runs nothing,
     * which is honest, rather than running work on a scheduler known to
     * fault under load.
     */

    /* Published last: the boot processor treats this as "usable", so
     * everything above must already be true. */
    percpu_self()->online = true;
    __asm__ volatile ("" ::: "memory");

    /* Idle. The processor has no current thread, so the first timer interrupt
     * takes it into the scheduler, which either hands it work off the shared
     * run queue or returns it here. Interrupts must be on for any of that,
     * and for IPIs to reach it at all. */
    __asm__ volatile ("sti");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/*
 * Delays here run off the TSC rather than the PIT.
 *
 * The INIT-SIPI sequence is timed, but by the time bring-up runs the timer
 * source is the LAPIC and pit_init() was never called, so pit_sleep_ms()
 * would wait on ticks that never arrive. The TSC is already calibrated by
 * cpu_init() and needs no interrupts, which also matters because this runs
 * with interrupts masked.
 */
static void smp_delay_us(uint64_t us)
{
    uint64_t hz = cpu_get_frequency();
    if (hz == 0) {
        /* No calibration: spin a fixed count rather than not delaying at all.
         * Too short a wait loses the AP; too long only slows boot. */
        for (volatile uint64_t i = 0; i < us * 1000ULL; i++) {
            __asm__ volatile ("pause");
        }
        return;
    }

    uint64_t target = cpu_get_time() + (hz / 1000000ULL) * us;
    while (cpu_get_time() < target) {
        __asm__ volatile ("pause");
    }
}

static bool ap_wait_online(uint32_t slot, uint32_t timeout_ms)
{
    for (uint32_t ms = 0; ms < timeout_ms; ms++) {
        if (percpu_peer(slot) != NULL) {
            return true;
        }
        smp_delay_us(1000);
    }
    return percpu_peer(slot) != NULL;
}

static bool start_one_ap(uint32_t slot, uint32_t apic_id)
{
    void* stack = kmalloc(AP_STACK_SIZE);
    if (!stack) {
        kprintf("[SMP] no stack for apic_id=%u\n", (unsigned)apic_id);
        return false;
    }

    uint64_t stack_top = ((uint64_t)(uintptr_t)stack + AP_STACK_SIZE) & ~0xFULL;

    g_ap_boot_slot = slot;
    g_ap_boot_apic_id = apic_id;
    tramp_patch(ap_trampoline_stack, stack_top);
    __asm__ volatile ("" ::: "memory");

    /* INIT, a pause, then STARTUP twice. The second STARTUP is not
     * superstition: the first can be lost on real hardware, and a processor
     * that already started ignores the repeat. */
    if (apic_send_init(apic_id) != 0) {
        return false;
    }
    smp_delay_us(10000);

    uint8_t start_page = (uint8_t)(AP_TRAMPOLINE_PHYS >> 12);
    if (apic_send_startup(apic_id, start_page) != 0) {
        return false;
    }
    smp_delay_us(SIPI_GAP_US);
    (void)apic_send_startup(apic_id, start_page);

    return ap_wait_online(slot, AP_ONLINE_TIMEOUT_MS);
}

/*
 * Confirm the processors that reported online are actually running.
 *
 * The online flag only says an AP reached the end of ap_entry. This sends
 * each one an IPI and waits for it to answer, which additionally proves its
 * LAPIC accepts delivery, its IDT is loaded, its interrupt entry path finds
 * the right per-CPU state and it can EOI. An AP that set the flag and then
 * wedged looks identical to a healthy one until something asks it a question.
 */
static volatile uint32_t g_ap_ping_replies = 0;

static void smp_ping_handler(interrupt_context_t* ctx)
{
    (void)ctx;
    __sync_fetch_and_add(&g_ap_ping_replies, 1);
}

int smp_verify_aps(void)
{
    uint32_t total = cpu_topology_count();
    uint32_t bsp = cpu_topology_bsp_index();

    int vector = interrupt_vector_alloc(0xF0, 0xFE);
    if (vector < 0) {
        return -1;
    }
    if (interrupt_register((uint32_t)vector, smp_ping_handler) != 0) {
        interrupt_vector_free((uint32_t)vector);
        return -1;
    }

    g_ap_ping_replies = 0;
    uint32_t targets = 0;

    for (uint32_t i = 0; i < total; i++) {
        if (i == bsp || percpu_peer(i) == NULL) {
            continue;
        }
        if (interrupt_send_ipi(i, (uint32_t)vector) == 0) {
            targets++;
        }
    }

    /* Bounded: an AP that never answers fails the check rather than
     * stalling the boot behind it. */
    for (uint32_t ms = 0; ms < 100 && g_ap_ping_replies < targets; ms++) {
        smp_delay_us(1000);
    }

    uint32_t answered = g_ap_ping_replies;
    interrupt_unregister((uint32_t)vector);
    interrupt_vector_free((uint32_t)vector);

    return (answered == targets) ? (int)answered : -1;
}

int smp_start_aps(void)
{
    uint32_t total = cpu_topology_count();
    if (total <= 1) {
        return 0;
    }

    g_kernel_pml4_phys = read_cr3();

    size_t blob_size = (size_t)(ap_trampoline_end - ap_trampoline_start);
    if (blob_size > 4096) {
        kprintf("[SMP] trampoline is %u bytes, will not fit its page\n",
                (unsigned)blob_size);
        return 0;
    }

    pmm_reserve_range(AP_TRAMPOLINE_PHYS, AP_TRAMPOLINE_PHYS + 4096);
    memcpy(X86_64_PHYS_TO_VIRT(AP_TRAMPOLINE_PHYS), ap_trampoline_start, blob_size);

    uint64_t tramp_pml4 = build_trampoline_pml4();
    if (!tramp_pml4) {
        kputs("[SMP] no page for the trampoline tables\n");
        return 0;
    }

    tramp_patch(ap_trampoline_cr3, tramp_pml4);
    tramp_patch(ap_trampoline_entry, (uint64_t)(uintptr_t)ap_entry);

    uint32_t bsp = cpu_topology_bsp_index();
    int started = 0;

    for (uint32_t i = 0; i < total; i++) {
        if (i == bsp) {
            continue;
        }

        const struct cpu_topology_entry* cpu = cpu_topology_get(i);
        if (!cpu || !cpu->enabled) {
            continue;
        }

        /* Serialised deliberately: the trampoline has one stack slot and one
         * "which slot am I" variable, and giving each processor its own would
         * buy nothing at boot. */
        if (start_one_ap(i, cpu->apic_id)) {
            g_online_count++;
            started++;
        } else {
            kprintf("[SMP] cpu%u (apic_id=%u) did not come online\n",
                    (unsigned)i, (unsigned)cpu->apic_id);
        }
    }

    return started;
}

uint32_t smp_online_count(void)
{
    return g_online_count;
}
