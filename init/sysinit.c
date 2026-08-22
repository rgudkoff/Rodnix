/**
 * @file sysinit.c
 * @brief Kernel staged subsystem initialization
 */

#include "../include/kernel.h"
#include "../include/console.h"
#include "../include/debug.h"
#include "../include/error.h"
#include "../trace/bootlog.h"
#include "../trace/startup_trace.h"
#include "../kernel/security.h"
#include "../trace/bootstrap.h"
#include "../kernel/loader.h"
#include "../kernel/kmod.h"
#include "../fs/vfs.h"
#include "../net/net.h"
#include "../kernel/syscall.h"
#include "../kernel/posix/posix_syscall.h"
#include "../kernel/arch/config.h"
#include "../kernel/arch/acpi.h"
#include "../kernel/arch/cpu_topology.h"
#include "../kernel/arch/percpu.h"
#include "../kernel/core/giant.h"
#include "../kernel/arch/gdt.h"
#include "../kernel/arch/x86_64/smp.h"
#include "../kernel/arch/syscall_fast.h"
#include "../include/gfx.h"

static bool g_timer_use_apic = false;

static int
run_sysinit_step(uint32_t subsystem, uint32_t order, const char* name, int (*fn)(void))
{
    int rc;

    if (!fn) {
        return RDNX_E_INVALID;
    }

    startup_trace_step_begin(subsystem, order, name);
    rc = fn();
    startup_trace_step_end(subsystem, order, name, rc);
    return rc;
}

const char*
kernel_timer_source_name(void)
{
    return g_timer_use_apic ? "lapic" : "pit";
}

static int
sysinit_cpu(void)
{
    extern int cpu_init(void);
    return cpu_init();
}

static int
sysinit_percpu(void)
{
    /* The very first step: from here on, "which processor am I" is a
     * GS-relative load rather than a hardcoded zero, and everything that
     * follows -- the GDT and TSS slot, the current task and thread -- can
     * ask honestly. */
    percpu_init_bsp();
    giant_init();
    return RDNX_OK;
}

static int
sysinit_ist(void)
{
    /* After memory_init, because the IST stacks come from the heap, and
     * still before interrupts are unmasked, because this rewrites live IDT
     * gates. */
    if (cpu_ist_init() != 0) {
        klog("cpu", "IST stacks unavailable, fatal exceptions stay on IST 0\n");
        return RDNX_OK;
    }
    klog("cpu", "IST stacks armed for #DF/NMI/#MC\n");
    return RDNX_OK;
}

static int
sysinit_interrupts(void)
{
    return interrupts_init();
}

static int
sysinit_memory(void)
{
    return memory_init();
}

static int
sysinit_apic(void)
{
    extern int apic_init(void);
    extern bool ioapic_is_available(void);

    if (apic_init() == 0) {
        if (ioapic_is_available()) {
            klog("apic", "LAPIC + I/O APIC ready\n");
        } else {
            klog("apic", "LAPIC ready, I/O APIC absent — PIC fallback for external IRQ\n");
        }
    } else {
        klog("apic", "init failed, falling back to PIC\n");
    }
    return RDNX_OK;
}

static int
sysinit_acpi(void)
{
    if (acpi_init() == 0) {
        klog("acpi", "tables discovered\n");
    } else {
        klog("acpi", "unavailable, using legacy fallbacks\n");
    }
    return RDNX_OK;
}

static int
sysinit_cpu_topology(void)
{
    /* Runs after apic_init: identifying which MADT entry is the boot
     * processor requires reading the live LAPIC ID. */
    if (cpu_topology_init() != 0) {
        klog("cpu", "processor inventory unavailable\n");
        return RDNX_OK;
    }

    const struct cpu_topology_entry* bsp =
        cpu_topology_get(cpu_topology_bsp_index());
    if (bsp) {
        percpu_bind_bsp(bsp->apic_id);
    }

    uint32_t total = cpu_topology_count();
    uint32_t startable = cpu_topology_startable_count();

    klog("cpu", "%u processor(s), %u startable, BSP apic_id=%u\n",
         (unsigned)total,
         (unsigned)startable,
         bsp ? (unsigned)bsp->apic_id : 0u);

    if (bootlog_is_verbose()) {
        cpu_topology_report();
    }
    return RDNX_OK;
}

static int
sysinit_ipi(void)
{
    extern int apic_ipi_selftest(void);
    extern bool apic_is_available(void);
    extern const char* lapic_access_mode_name(void);

    if (!apic_is_available()) {
        klog("apic", "no LAPIC, IPI unavailable\n");
        return RDNX_OK;
    }

    if (apic_ipi_selftest() == 0) {
        klog("apic", "IPI delivery verified (%s)\n", lapic_access_mode_name());
    } else {
        klog("apic", "IPI self-test FAILED (%s)\n", lapic_access_mode_name());
    }
    return RDNX_OK;
}

static int
sysinit_timer(void)
{
    extern bool apic_is_available(void);
    extern int apic_timer_init(uint32_t frequency);
    extern int pit_init(uint32_t frequency);

    bool use_apic_timer = false;
    if (apic_is_available()) {
        if (apic_timer_init(1000) == 0) {
            use_apic_timer = true;
        } else if (bootlog_is_verbose()) {
            klog("timer", "LAPIC timer init failed, trying PIT\n");
        }
    }
    if (!use_apic_timer) {
        if (pit_init(1000) != 0) {
            return RDNX_E_GENERIC;
        }
    }

    g_timer_use_apic = use_apic_timer;
    klog("timer", "source: %s @ 1000 Hz\n", use_apic_timer ? "LAPIC" : "PIT");
    bootlog_mark("timer", use_apic_timer ? "lapic" : "pit");
    return RDNX_OK;
}

static int
sysinit_smp(void)
{
    /* After the timer, because the INIT-SIPI sequence is timed, and before
     * the scheduler, so the processor count it eventually sees is final. */
    uint32_t expected = cpu_topology_startable_count();
    int started = smp_start_aps();

    if (expected <= 1) {
        klog("smp", "uniprocessor\n");
        return RDNX_OK;
    }

    klog("smp", "%u of %u processors online\n",
         (unsigned)smp_online_count(), (unsigned)expected);

    if (started > 0) {
        int answered = smp_verify_aps();
        if (answered == started) {
            klog("smp", "%d application processor(s) answered an IPI\n", answered);
        } else {
            klog("smp", "IPI verification FAILED (%d of %d answered)\n",
                 answered, started);
        }
    }
    return RDNX_OK;
}

static int
sysinit_scheduler(void)
{
    return scheduler_init();
}

static int
sysinit_ipc(void)
{
    return ipc_init();
}

static int
sysinit_syscalls(void)
{
    syscall_init();
    if (x86_64_syscall_fast_init() == 0) {
        klog("syscall", "fast syscall/sysret path enabled\n");
    } else {
        klog("syscall", "fast path init failed, using int 0x80\n");
    }
    return RDNX_OK;
}

static int
sysinit_security(void)
{
    security_init();
    return RDNX_OK;
}

static int
sysinit_loader(void)
{
    loader_init();
    return RDNX_OK;
}

static int
sysinit_kmod(void)
{
    if (kmod_init() != RDNX_OK) {
        klog("kmod", "registry init failed\n");
        return RDNX_E_GENERIC;
    }
    return RDNX_OK;
}

static int
sysinit_fabric(void)
{
    extern void fabric_init(void);
    extern void usb_init(void);
    extern void virt_bus_init(void);
    extern void pci_bus_init(void);
    extern void usb_bus_init(void);
    extern void ps2_bus_init(void);
    extern void hid_kbd_init(void);
    extern void usb_host_pci_init(void);
    extern void usb_hid_init(void);
    extern void usb_msc_init(void);
    extern void virtio_net_stub_init(void);
    extern void e1000_net_stub_init(void);
    extern void vga_display_stub_init(void);
    extern void mb2_fb_init(void);
    extern void bochs_vbe_init(void);
    extern void ide_storage_stub_init(void);
    extern int fabric_block_service_init(void);
    extern void fabric_platform_services_init(void);
    extern void gfx_init(void);

    gfx_init();

    fabric_init();
    usb_init();
    virt_bus_init();
    pci_bus_init();
    usb_bus_init();
    ps2_bus_init();
    hid_kbd_init();
    usb_host_pci_init();
    usb_hid_init();
    usb_msc_init();
    virtio_net_stub_init();
    e1000_net_stub_init();
    bochs_vbe_init();
    mb2_fb_init();
    vga_display_stub_init();

    if (fabric_block_service_init() != 0) {
        klog("fabric", "block service init failed\n");
        return RDNX_E_GENERIC;
    }

    ide_storage_stub_init();
    fabric_platform_services_init();

    klog("fabric", "buses: virt pci usb ps2 | drivers: mb2_fb hid_kbd usb_host_pci usb_hid usb_msc virtio_net e1000 bochs_vbe vga ide\n");
    return RDNX_OK;
}

static int
sysinit_vfs(void)
{
    boot_info_t* bi = boot_get_info();
    if (bi && bi->initrd_start && bi->initrd_size) {
        void* initrd = ARCH_PHYS_TO_VIRT(bi->initrd_start);
        vfs_set_initrd(initrd, (size_t)bi->initrd_size);
        if (bootlog_is_verbose()) {
            klog("vfs", "initrd: start=%llx size=%llu\n",
                (unsigned long long)bi->initrd_start,
                (unsigned long long)bi->initrd_size);
        }
    }
    if (vfs_init() != 0) {
        klog("vfs", "init failed\n");
        return RDNX_E_GENERIC;
    }
    (void)vfs_mkdir("/mnt");
    /*
     * Mount order for ext2 data partition:
     *   1. disk0p2 — second partition of first disk (real hardware: USB/HDD layout)
     *   2. disk0p1 — first partition (single-partition setups)
     *   3. disk0   — raw disk, no partition table (QEMU raw ext2 image)
     *   4. disk1   — second IDE disk (legacy QEMU two-disk setups)
     */
    static const char* const mount_candidates[] = {
        "disk0p2", "disk0p1", "disk0", "disk1", NULL
    };
    int mrc = RDNX_E_NOTFOUND;
    for (int ci = 0; mount_candidates[ci] != NULL; ci++) {
        mrc = vfs_mount("ext2", mount_candidates[ci], "/mnt");
        if (mrc == RDNX_OK) {
            klog("vfs", "ext2 %s mounted at /mnt\n", mount_candidates[ci]);
            break;
        }
    }
    if (mrc != RDNX_OK) {
        klog("vfs", "ext2 mount skipped — no suitable device found\n");
    }
    return RDNX_OK;
}

static int
sysinit_net(void)
{
    if (net_init() != 0) {
        klog("net", "init failed\n");
        return RDNX_E_GENERIC;
    }
    return RDNX_OK;
}

void
kernel_run_bootstrap_sysinit(void)
{
    /* Per-CPU state comes first: gdt_init() inside cpu_init() picks its GDT
     * and TSS slot by asking which processor it is running on. */
    if (run_sysinit_step(SI_SUB_CPU, SI_ORDER_FIRST, "percpu_init", sysinit_percpu) != 0) {
        panic("Per-CPU init failed");
    }

    if (run_sysinit_step(SI_SUB_CPU, SI_ORDER_SECOND, "cpu_init", sysinit_cpu) != 0) {
        panic("CPU init failed");
    }
    klog("cpu", "ready\n");

    if (run_sysinit_step(SI_SUB_INTR, SI_ORDER_FIRST, "interrupts_init", sysinit_interrupts) != 0) {
        panic("Interrupts init failed");
    }
    klog("interrupts", "IDT loaded\n");

    if (run_sysinit_step(SI_SUB_VM, SI_ORDER_FIRST, "memory_init", sysinit_memory) != 0) {
        panic("Memory init failed");
    }
    klog("memory", "PMM + VM ready\n");

    if (run_sysinit_step(SI_SUB_CPU, SI_ORDER_THIRD, "ist_init", sysinit_ist) != 0) {
        panic("IST init failed");
    }

    if (run_sysinit_step(SI_SUB_INTR, SI_ORDER_SECOND, "acpi_init", sysinit_acpi) != 0) {
        panic("ACPI init failed");
    }

    if (run_sysinit_step(SI_SUB_INTR, SI_ORDER_THIRD, "apic_init", sysinit_apic) != 0) {
        panic("APIC init failed");
    }

    if (run_sysinit_step(SI_SUB_INTR, SI_ORDER_MIDDLE, "cpu_topology_init",
                         sysinit_cpu_topology) != 0) {
        panic("CPU topology init failed");
    }

    if (run_sysinit_step(SI_SUB_INTR, SI_ORDER_ANY, "ipi_selftest", sysinit_ipi) != 0) {
        panic("IPI self-test step failed");
    }

    if (run_sysinit_step(SI_SUB_CLOCKS, SI_ORDER_FIRST, "timer_init", sysinit_timer) != 0) {
        panic("Timer init failed");
    }

    if (run_sysinit_step(SI_SUB_CLOCKS, SI_ORDER_SECOND, "smp_start_aps", sysinit_smp) != 0) {
        panic("SMP bring-up step failed");
    }

    if (run_sysinit_step(SI_SUB_SCHED, SI_ORDER_FIRST, "scheduler_init", sysinit_scheduler) != 0) {
        panic("Scheduler init failed");
    }
    klog("sched", "ready\n");

    if (run_sysinit_step(SI_SUB_SCHED, SI_ORDER_SECOND, "ipc_init", sysinit_ipc) != 0) {
        panic("IPC init failed");
    }
    klog("ipc", "ready\n");

    if (run_sysinit_step(SI_SUB_SYSCALLS, SI_ORDER_FIRST, "syscall_init", sysinit_syscalls) != 0) {
        panic("Syscall init failed");
    }

    if (run_sysinit_step(SI_SUB_SECURITY, SI_ORDER_FIRST, "security_init", sysinit_security) != 0) {
        panic("Security init failed");
    }
    klog("security", "policy loaded\n");

    if (run_sysinit_step(SI_SUB_KTHREAD, SI_ORDER_FIRST, "loader_init", sysinit_loader) != 0) {
        panic("Loader init failed");
    }
    klog("loader", "ELF loader ready\n");

    if (run_sysinit_step(SI_SUB_DRIVERS, SI_ORDER_FIRST, "kmod_init", sysinit_kmod) != 0) {
        panic("Kmod init failed");
    }
    klog("kmod", "registry ready\n");

    if (run_sysinit_step(SI_SUB_DRIVERS, SI_ORDER_SECOND, "fabric_init", sysinit_fabric) != 0) {
        panic("Fabric init failed");
    }

    if (run_sysinit_step(SI_SUB_VFS, SI_ORDER_FIRST, "vfs_init", sysinit_vfs) != 0) {
        panic("VFS init failed");
    }

    if (run_sysinit_step(SI_SUB_PROTO, SI_ORDER_FIRST, "net_init", sysinit_net) != 0) {
        panic("Net init failed");
    }
    klog("net", "stack ready\n");
}

void
kernel_enable_runtime_interrupts(void)
{
    extern void apic_timer_stop(void);
    extern void pit_disable(void);
    extern void apic_timer_start(void);
    extern void pit_enable(void);
    extern uint64_t scheduler_get_ticks(void);
    extern uint32_t apic_timer_get_ticks(void);
    extern uint32_t apic_timer_get_current_count(void);
    extern int pit_init(uint32_t frequency);

    bootlog_mark("interrupts", "enable_enter");
    __asm__ volatile ("" ::: "memory");

    if (g_timer_use_apic) {
        apic_timer_stop();
    } else {
        pit_disable();
    }
    __asm__ volatile ("" ::: "memory");

    /* IRQL is per-CPU now; set this processor's own. */
    (void)set_irql(IRQL_PASSIVE);
    __asm__ volatile ("" ::: "memory");

    __asm__ volatile ("sti");
    __asm__ volatile ("" ::: "memory");

    if (g_timer_use_apic) {
        apic_timer_start();
    } else {
        pit_enable();
    }
    __asm__ volatile ("" ::: "memory");

    for (volatile int i = 0; i < 10000; i++) {
        __asm__ volatile ("pause");
    }
    __asm__ volatile ("" ::: "memory");

    {
        extern int irql_selftest(void);
        if (irql_selftest() == 0) {
            klog("irq", "IRQL masking verified via LAPIC TPR\n");
        } else {
            klog("irq", "IRQL masking self-test FAILED\n");
        }
    }

    if (g_timer_use_apic) {
        uint64_t t0 = scheduler_get_ticks();
        uint32_t ap0 = apic_timer_get_ticks();
        uint32_t cur0 = apic_timer_get_current_count();
        for (volatile int i = 0; i < 5000000; i++) {
            __asm__ volatile ("pause");
        }
        uint64_t t1 = scheduler_get_ticks();
        uint32_t ap1 = apic_timer_get_ticks();
        uint32_t cur1 = apic_timer_get_current_count();
        bool apic_progress = (ap1 > ap0) || (cur1 != cur0);
        bool sched_progress = (t1 > t0);
        if (!apic_progress && !sched_progress) {
            if (bootlog_is_verbose()) {
                extern uint32_t apic_timer_get_frequency(void);
                klog("timer", "LAPIC stalled: sched=%llu->%llu apic=%u->%u hz=%u\n",
                    (unsigned long long)t0, (unsigned long long)t1,
                    ap0, ap1, apic_timer_get_frequency());
            }
            apic_timer_stop();
            if (pit_init(1000) == 0) {
                pit_enable();
                g_timer_use_apic = false;
                klog("timer", "LAPIC stalled — fell back to PIT @ 1000 Hz\n");
            } else {
                klog("timer", "LAPIC stalled and PIT fallback failed\n");
            }
        }
    }

    klog("kernel", "interrupts enabled\n");
    bootlog_mark("interrupts", "enable_done");
    __asm__ volatile ("" ::: "memory");
}
