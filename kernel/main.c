/**
 * @file main.c
 * @brief Kernel main entry point
 */

#include "../include/kernel.h"
#include "../include/console.h"
#include "../include/debug.h"
#include "../include/version.h"
#include "../include/error.h"
#include "core/interrupts.h"
#include "syscall.h"
#include "posix/posix_syscall.h"
#include "fs/vfs.h"
#include "net/net.h"
#include "common/security.h"
#include "common/bootstrap.h"
#include "common/loader.h"
#include "common/kmod.h"
#include "common/shell.h"
#include "common/bootlog.h"
#include "common/startup_trace.h"
#include "common/idl_demo.h"
#include "core/boot.h"
#include "arch/config.h"
#include "arch/acpi.h"
#include "arch/syscall_fast.h"
#include "../include/common.h"

#define USER_INIT_PATH_MAX 128

static char g_user_init_path[USER_INIT_PATH_MAX] = "/bin/init";
static bool g_timer_use_apic = false;

static int run_sysinit_step(uint32_t subsystem,
                            uint32_t order,
                            const char* name,
                            int (*fn)(void))
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

const char* kernel_timer_source_name(void)
{
    return g_timer_use_apic ? "lapic" : "pit";
}

static bool bootarg_has_token(const char* cmdline, const char* token)
{
    if (!cmdline || !token || token[0] == '\0') {
        return false;
    }

    size_t tok_len = strlen(token);
    const char* p = cmdline;

    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char* start = p;
        while (*p && *p != ' ') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len == tok_len && strncmp(start, token, tok_len) == 0) {
            return true;
        }
    }

    return false;
}

static void bootarg_pick_init_path(char* out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }

    const char* fallback = "/bin/init";
    size_t i = 0;
    while (fallback[i] && i + 1 < out_len) {
        out[i] = fallback[i];
        i++;
    }
    out[i] = '\0';

    boot_info_t* bi = boot_get_info();
    if (!bi || !bi->cmdline[0]) {
        return;
    }

    const char* cmdline = bi->cmdline;
    const char* key = "rdnx.init=";
    size_t key_len = strlen(key);
    const char* p = cmdline;

    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char* start = p;
        while (*p && *p != ' ') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len > key_len && strncmp(start, key, key_len) == 0) {
            size_t path_len = len - key_len;
            if (path_len >= out_len) {
                path_len = out_len - 1;
            }
            memcpy(out, start + key_len, path_len);
            out[path_len] = '\0';
            return;
        }
    }
}

static void idle_thread(void* arg)
{
    (void)arg;
    for (;;) {
        __asm__ volatile ("sti; hlt" ::: "memory");
    }
}

static void kernel_shell_thread(void* arg)
{
    (void)arg;
    if (shell_init() != 0) {
        panic("Shell init failed");
    }
    shell_run();
    for (;;) {
        cpu_idle();
    }
}

static void user_init_thread(void* arg)
{
    const char* init_path = (const char*)arg;
    if (!init_path || init_path[0] == '\0') {
        init_path = "/bin/init";
    }

    klog("init", "exec %s\n", init_path);
    int ret = loader_exec(init_path);
    if (ret == 0) {
        /* usermode_enter should not return on success */
        for (;;) {
            cpu_idle();
        }
    }

    klog("init", "exec failed (%d), starting kernel shell\n", ret);
    if (shell_init() != 0) {
        panic("Shell fallback init failed");
    }
    shell_run();
    for (;;) {
        cpu_idle();
    }
}

static int sysinit_cpu(void)
{
    extern int cpu_init(void);
    return cpu_init();
}

static int sysinit_interrupts(void)
{
    return interrupts_init();
}

static int sysinit_memory(void)
{
    return memory_init();
}

static int sysinit_apic(void)
{
    extern int apic_init(void);
    extern bool apic_is_available(void);
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

static int sysinit_acpi(void)
{
    if (acpi_init() == 0) {
        klog("acpi", "tables discovered\n");
    } else {
        klog("acpi", "unavailable, using legacy fallbacks\n");
    }
    return RDNX_OK;
}

static int sysinit_timer(void)
{
    extern bool apic_is_available(void);
    extern int apic_timer_init(uint32_t frequency);
    extern int pit_init(uint32_t frequency);

    bool use_apic_timer = false;
    if (apic_is_available()) {
        if (apic_timer_init(100) == 0) {
            use_apic_timer = true;
        } else if (bootlog_is_verbose()) {
            klog("timer", "LAPIC timer init failed, trying PIT\n");
        }
    }
    if (!use_apic_timer) {
        if (pit_init(100) != 0) {
            return RDNX_E_GENERIC;
        }
    }

    g_timer_use_apic = use_apic_timer;
    klog("timer", "source: %s @ 100 Hz\n", use_apic_timer ? "LAPIC" : "PIT");
    bootlog_mark("timer", use_apic_timer ? "lapic" : "pit");
    return RDNX_OK;
}

static int sysinit_scheduler(void)
{
    return scheduler_init();
}

static int sysinit_ipc(void)
{
    return ipc_init();
}

static int sysinit_syscalls(void)
{
    syscall_init();
    if (x86_64_syscall_fast_init() == 0) {
        klog("syscall", "fast syscall/sysret path enabled\n");
    } else {
        klog("syscall", "fast path init failed, using int 0x80\n");
    }
    return RDNX_OK;
}

static int sysinit_security(void)
{
    security_init();
    return RDNX_OK;
}

static int sysinit_loader(void)
{
    loader_init();
    return RDNX_OK;
}

static int sysinit_kmod(void)
{
    if (kmod_init() != RDNX_OK) {
        klog("kmod", "registry init failed\n");
        return RDNX_E_GENERIC;
    }
    return RDNX_OK;
}

static int sysinit_fabric(void)
{
    extern void fabric_init(void);
    extern void virt_bus_init(void);
    extern void pci_bus_init(void);
    extern void ps2_bus_init(void);
    extern void hid_kbd_init(void);
    extern void virtio_net_stub_init(void);
    extern void e1000_net_stub_init(void);
    extern void vga_display_stub_init(void);
    extern void ide_storage_stub_init(void);
    extern int fabric_block_service_init(void);
    extern void fabric_platform_services_init(void);

    fabric_init();
    virt_bus_init();
    pci_bus_init();
    ps2_bus_init();
    hid_kbd_init();
    virtio_net_stub_init();
    e1000_net_stub_init();
    vga_display_stub_init();

    if (fabric_block_service_init() != 0) {
        klog("fabric", "block service init failed\n");
        return RDNX_E_GENERIC;
    }

    ide_storage_stub_init();
    fabric_platform_services_init();
    klog("fabric", "buses: virt pci ps2 | drivers: hid_kbd virtio_net e1000 vga ide\n");
    return RDNX_OK;
}

static int sysinit_vfs(void)
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
    int mrc = vfs_mount("ext2", "disk0", "/mnt");
    if (mrc == 0) {
        klog("vfs", "ext2 disk0 mounted at /mnt\n");
    } else {
        klog("vfs", "ext2 mount skipped (rc=%d)\n", mrc);
    }
    return RDNX_OK;
}

static int sysinit_net(void)
{
    if (net_init() != 0) {
        klog("net", "init failed\n");
        return RDNX_E_GENERIC;
    }
    return RDNX_OK;
}

/**
 * Kernel main function
 * @param magic Multiboot2 magic number
 * @param mbi Multiboot2 information structure
 */
void kmain(uint32_t magic, void* mbi)
{
    console_set_vga_buffer((void*)0xB8000);
    console_init();
    console_set_log_prefix_enabled(false);

    kputs("\n");
    kputs("  RodNIX " RODNIX_RELEASE " — 64-bit experimental Unix\n");
    kputs("\n");

    bootlog_mark("kmain", "enter");

    /* Boot subsystem */
    boot_info_t boot_info;
    boot_info.magic = magic;
    boot_info.boot_info = mbi;
    boot_info.mem_lower = 0;
    boot_info.mem_upper = 0;
    boot_info.cmdline[0] = '\0';
    boot_info.flags = 0;

    bootlog_mark("boot", "enter");
    if (boot_early_init(&boot_info) != 0) {
        bootlog_mark("boot", "fail");
        panic("Boot init failed");
    }
    startup_trace_init(boot_info.cmdline);
    bootlog_init();
    bootlog_mark("boot", "done");

    /* Core subsystems */
    if (run_sysinit_step(SI_SUB_CPU, SI_ORDER_FIRST, "cpu_init", sysinit_cpu) != 0) {
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

    if (run_sysinit_step(SI_SUB_INTR, SI_ORDER_SECOND, "acpi_init", sysinit_acpi) != 0) {
        panic("ACPI init failed");
    }

    if (run_sysinit_step(SI_SUB_INTR, SI_ORDER_THIRD, "apic_init", sysinit_apic) != 0) {
        panic("APIC init failed");
    }

    if (run_sysinit_step(SI_SUB_CLOCKS, SI_ORDER_FIRST, "timer_init", sysinit_timer) != 0) {
        panic("Timer init failed");
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

    /* Enable interrupts */
    bootlog_mark("interrupts", "enable_enter");
    __asm__ volatile ("" ::: "memory");

    extern void apic_timer_stop(void);
    extern void pit_disable(void);
    if (g_timer_use_apic) {
        apic_timer_stop();
    } else {
        pit_disable();
    }
    __asm__ volatile ("" ::: "memory");

    extern volatile irql_t current_irql;
    current_irql = IRQL_PASSIVE;
    __asm__ volatile ("" ::: "memory");

    __asm__ volatile ("sti");
    __asm__ volatile ("" ::: "memory");

    extern void apic_timer_start(void);
    extern void pit_enable(void);
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

    /* Validate APIC timer progress; fall back to PIT if stalled */
    if (g_timer_use_apic) {
        extern uint64_t scheduler_get_ticks(void);
        extern uint32_t apic_timer_get_ticks(void);
        extern uint32_t apic_timer_get_current_count(void);
        extern int pit_init(uint32_t frequency);
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
                extern uint32_t apic_timer_get_lvt_raw(void);
                extern uint32_t apic_timer_get_initial_count(void);
                klog("timer", "LAPIC stalled: sched=%llu->%llu apic=%u->%u hz=%u\n",
                        (unsigned long long)t0, (unsigned long long)t1,
                        ap0, ap1, apic_timer_get_frequency());
            }
            apic_timer_stop();
            if (pit_init(100) == 0) {
                pit_enable();
                g_timer_use_apic = false;
                klog("timer", "LAPIC stalled — fell back to PIT\n");
            } else {
                klog("timer", "LAPIC stalled and PIT fallback failed\n");
            }
        }
    }

    klog("kernel", "interrupts enabled\n");
    bootlog_mark("interrupts", "enable_done");
    __asm__ volatile ("" ::: "memory");

    /* Bootstrap mode selection */
    bool force_kernel_shell = false;
    boot_info_t* boot_cfg = boot_get_info();
    if (boot_cfg) {
        force_kernel_shell =
            bootarg_has_token(boot_cfg->cmdline, "rdnx.shell=1") ||
            bootarg_has_token(boot_cfg->cmdline, "shell=1");
    }
    bootarg_pick_init_path(g_user_init_path, sizeof(g_user_init_path));

    bootlog_mark("shell", "enter");
    __asm__ volatile ("" ::: "memory");

    /* Threads */
    bootlog_mark("threads", "create_enter");
    __asm__ volatile ("" ::: "memory");

    task_t* kernel_task = task_create();
    if (!kernel_task) {
        bootlog_mark("threads", "kernel_task_fail");
        panic("Kernel task create failed");
    }
    kernel_task->state = TASK_STATE_READY;
    task_set_current(kernel_task);

    thread_t* primary = NULL;
    if (force_kernel_shell) {
        klog("init", "kernel shell forced by boot arg\n");
        primary = thread_create(kernel_task, kernel_shell_thread, NULL);
    } else {
        klog("init", "userspace target: %s\n", g_user_init_path);
        task_t* init_task = task_create();
        if (!init_task) {
            bootlog_mark("threads", "kernel_task_fail");
            panic("Init task create failed");
        }
        init_task->state = TASK_STATE_READY;
        if (posix_bind_stdio_to_console(init_task) != 0) {
            bootlog_mark("threads", "thread_create_fail");
            panic("Init stdio bind failed");
        }
        primary = thread_create(init_task, user_init_thread, (void*)g_user_init_path);
    }
    thread_t* idle = thread_create(kernel_task, idle_thread, NULL);
    if (idle) {
        idle->priority = PRIORITY_MIN;
    }
    bootstrap_start();
    /* Keep IDL demo disabled in baseline boot path; it perturbs contract CI. */
    /* idl_demo_start(); */
    if (!primary || !idle) {
        bootlog_mark("threads", "thread_create_fail");
        panic("Kernel thread create failed");
    }

    scheduler_add_thread(primary);
    scheduler_add_thread(idle);
    bootlog_mark("threads", "created");

    klog("kernel", "boot complete — starting scheduler\n");
    kputs("\n");
    bootlog_mark("scheduler", "start");
    __asm__ volatile ("" ::: "memory");
    scheduler_start();

    /* Should never reach here */
    for (;;) {
        interrupt_wait();
    }
}
