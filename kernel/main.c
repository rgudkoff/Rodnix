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
#include "arch/x86_64/config.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/syscall_fast.h"
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

    kprintf("[INIT-USER] exec %s\n", init_path);
    int ret = loader_exec(init_path);
    if (ret == 0) {
        /* usermode_enter should not return on success */
        for (;;) {
            cpu_idle();
        }
    }

    kprintf("[INIT-USER] exec failed (%d)\n", ret);
    kputs("[DEGRADED] userland init unavailable, starting kernel shell fallback\n");
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
    if (apic_init() == 0) {
        kputs("[INIT-5.1] APIC initialized\n");
    } else {
        kputs("[INIT-5.2] APIC init failed, fallback to PIC\n");
    }

    extern bool apic_is_available(void);
    if (apic_is_available()) {
        extern bool ioapic_is_available(void);
        if (ioapic_is_available()) {
            kputs("[INIT-5.3] I/O APIC available, PIC masked (APIC mode)\n");
        } else {
            kputs("[INIT-5.3] LAPIC available, I/O APIC not - PIC fallback for external IRQ\n");
            kputs("[DEGRADED] External IRQ routing stays on PIC fallback path\n");
        }
    }
    return RDNX_OK;
}

static int sysinit_acpi(void)
{
    if (acpi_init() == 0) {
        kputs("[INIT-4.5] ACPI tables discovered\n");
    } else {
        kputs("[INIT-4.5] ACPI unavailable, continue with legacy fallbacks\n");
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
        kputs("[INIT-6.1] Use LAPIC timer\n");
        if (apic_timer_init(100) == 0) {
            use_apic_timer = true;
        } else {
            kputs("[INIT-6.1.1] LAPIC timer failed, fallback to PIT\n");
        }
    }
    if (!use_apic_timer) {
        kputs("[INIT-6.2] Use PIT\n");
        if (pit_init(100) != 0) {
            return RDNX_E_GENERIC;
        }
    }

    g_timer_use_apic = use_apic_timer;
    if (use_apic_timer) {
        bootlog_mark("timer", "lapic");
        kputs("[INIT-6.9] Timer source: LAPIC\n");
    } else {
        bootlog_mark("timer", "pit");
        kputs("[INIT-6.9] Timer source: PIT\n");
    }
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
        kputs("[INIT-8.5a] syscall/sysret fast path enabled\n");
    } else {
        kputs("[INIT-8.5a] syscall/sysret fast path init failed, int 0x80 fallback\n");
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
        kputs("[INIT-8.8] Kmod init failed\n");
        return RDNX_E_GENERIC;
    }
    kputs("[INIT-8.8] Kmod registry ready\n");
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
    kputs("[INIT-9.1] Fabric initialized\n");
    virt_bus_init();
    kputs("[INIT-9.2] Virt bus initialized\n");
    pci_bus_init();
    kputs("[INIT-9.3] PCI bus initialized\n");
    ps2_bus_init();
    kputs("[INIT-9.4] PS/2 bus initialized\n");
    hid_kbd_init();
    kputs("[INIT-9.5] HID keyboard driver initialized\n");
    virtio_net_stub_init();
    kputs("[INIT-9.5a] Virtio-net stub driver initialized\n");
    e1000_net_stub_init();
    kputs("[INIT-9.5b] e1000-net stub driver initialized\n");
    vga_display_stub_init();
    kputs("[INIT-9.5c] VGA display stub driver initialized\n");

    if (fabric_block_service_init() != 0) {
        kputs("[INIT-9.5d] Block service init failed\n");
        return RDNX_E_GENERIC;
    }
    kputs("[INIT-9.5d] Block service initialized\n");

    ide_storage_stub_init();
    kputs("[INIT-9.5e] IDE storage stub driver initialized\n");
    fabric_platform_services_init();
    kputs("[INIT-9.5f] Platform services initialized\n");
    kputs("[INIT-9-OK] Fabric initialization complete\n");
    return RDNX_OK;
}

static int sysinit_vfs(void)
{
    boot_info_t* bi = boot_get_info();
    if (bi && bi->initrd_start && bi->initrd_size) {
        kprintf("[INIT-9.6] initrd: start=%llx size=%llu\n",
                (unsigned long long)bi->initrd_start,
                (unsigned long long)bi->initrd_size);
        void* initrd = X86_64_PHYS_TO_VIRT(bi->initrd_start);
        vfs_set_initrd(initrd, (size_t)bi->initrd_size);
    }
    if (vfs_init() != 0) {
        kputs("[INIT-9.6] VFS init failed\n");
        return RDNX_E_GENERIC;
    }
    kputs("[INIT-9.6] VFS ready\n");
    (void)vfs_mkdir("/mnt");
    int mrc = vfs_mount("ext2", "disk0", "/mnt");
    if (mrc == 0) {
        kputs("[INIT-9.6a] Mounted ext2 disk0 at /mnt\n");
    } else {
        kprintf("[INIT-9.6a] ext2 mount skipped rc=%d\n", mrc);
    }
    return RDNX_OK;
}

static int sysinit_net(void)
{
    if (net_init() != 0) {
        kputs("[INIT-9.7] Net init failed\n");
        return RDNX_E_GENERIC;
    }
    kputs("[INIT-9.7] Net ready (stub)\n");
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
    
    /* Print welcome message */
    kputs("========================================\n");
    kputs("    RodNIX Kernel v" RODNIX_RELEASE "\n");
    kputs("    64-bit Architecture\n");
    kputs("========================================\n\n");
    
    kputs("[INIT] Starting kernel...\n");
    bootlog_mark("kmain", "enter");

    /* Step 1: Initialize boot subsystem */
    kputs("[INIT] Boot subsystem\n");
    boot_info_t boot_info;
    boot_info.magic = magic;
    boot_info.boot_info = mbi;
    boot_info.mem_lower = 0;
    boot_info.mem_upper = 0;
    /* Initialize cmdline buffer to empty string (fixed buffer) */
    boot_info.cmdline[0] = '\0';
    boot_info.flags = 0;

    bootlog_mark("boot", "enter");
    int boot_result = boot_early_init(&boot_info);
    if (boot_result != 0) {
        bootlog_mark("boot", "fail");
        kputs("[INIT-ERR] Boot init failed\n");
        panic("Boot init failed");
    }
    startup_trace_init(boot_info.cmdline);
    bootlog_init();
    bootlog_mark("boot", "done");
    kputs("[INIT] Boot done\n");
    
    if (run_sysinit_step(SI_SUB_CPU, SI_ORDER_FIRST, "cpu_init", sysinit_cpu) != 0) {
        panic("CPU init failed");
    }
    if (run_sysinit_step(SI_SUB_INTR, SI_ORDER_FIRST, "interrupts_init", sysinit_interrupts) != 0) {
        panic("Interrupts init failed");
    }
    if (run_sysinit_step(SI_SUB_VM, SI_ORDER_FIRST, "memory_init", sysinit_memory) != 0) {
        panic("Memory init failed");
    }
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
    if (run_sysinit_step(SI_SUB_SCHED, SI_ORDER_SECOND, "ipc_init", sysinit_ipc) != 0) {
        panic("IPC init failed");
    }
    if (run_sysinit_step(SI_SUB_SYSCALLS, SI_ORDER_FIRST, "syscall_init", sysinit_syscalls) != 0) {
        panic("Syscall init failed");
    }
    if (run_sysinit_step(SI_SUB_SECURITY, SI_ORDER_FIRST, "security_init", sysinit_security) != 0) {
        panic("Security init failed");
    }
    if (run_sysinit_step(SI_SUB_KTHREAD, SI_ORDER_FIRST, "loader_init", sysinit_loader) != 0) {
        panic("Loader init failed");
    }
    if (run_sysinit_step(SI_SUB_DRIVERS, SI_ORDER_FIRST, "kmod_init", sysinit_kmod) != 0) {
        panic("Kmod init failed");
    }
    if (run_sysinit_step(SI_SUB_DRIVERS, SI_ORDER_SECOND, "fabric_init", sysinit_fabric) != 0) {
        panic("Fabric init failed");
    }
    if (run_sysinit_step(SI_SUB_VFS, SI_ORDER_FIRST, "vfs_init", sysinit_vfs) != 0) {
        panic("VFS init failed");
    }
    if (run_sysinit_step(SI_SUB_PROTO, SI_ORDER_FIRST, "net_init", sysinit_net) != 0) {
        panic("Net init failed");
    }
    
    /* Step 10: Enable interrupts (set IRQL to PASSIVE) */
    kputs("[INIT-10] Enable interrupts\n");
    bootlog_mark("interrupts", "enable_enter");
    __asm__ volatile ("" ::: "memory");
    
    /* Temporarily disable timer to avoid immediate interrupt on sti */
    kputs("[INIT-10.1] Disable timer\n");
    __asm__ volatile ("" ::: "memory");
    extern void apic_timer_stop(void);
    extern void pit_disable(void);
    if (g_timer_use_apic) {
        apic_timer_stop();
    } else {
        pit_disable();
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Set IRQL to PASSIVE */
    kputs("[INIT-10.2] Set IRQL\n");
    __asm__ volatile ("" ::: "memory");
    extern volatile irql_t current_irql;
    current_irql = IRQL_PASSIVE;
    __asm__ volatile ("" ::: "memory");
    
    /* Enable interrupts */
    kputs("[INIT-10.3] Execute sti\n");
    __asm__ volatile ("" ::: "memory");
    __asm__ volatile ("sti");
    __asm__ volatile ("" ::: "memory");
    
    /* Re-enable timer after interrupts are enabled */
    kputs("[INIT-10.4] Enable timer\n");
    __asm__ volatile ("" ::: "memory");
    extern void apic_timer_start(void);
    extern void pit_enable(void);
    if (g_timer_use_apic) {
        apic_timer_start();
    } else {
        pit_enable();
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Small delay to allow any pending interrupts to be processed */
    kputs("[INIT-10.5] Delay after PIT enable\n");
    __asm__ volatile ("" ::: "memory");
    for (volatile int i = 0; i < 10000; i++) {
        __asm__ volatile ("pause");
    }
    __asm__ volatile ("" ::: "memory");

    /* Validate timer progress and fallback to PIT if APIC timer is not ticking. */
    if (g_timer_use_apic) {
        extern uint64_t scheduler_get_ticks(void);
        extern uint32_t apic_timer_get_ticks(void);
        extern uint32_t apic_timer_get_frequency(void);
        extern uint32_t apic_timer_get_lvt_raw(void);
        extern uint32_t apic_timer_get_initial_count(void);
        extern uint32_t apic_timer_get_current_count(void);
        extern int pit_init(uint32_t frequency);
        uint64_t t0 = scheduler_get_ticks();
        uint32_t ap0 = apic_timer_get_ticks();
        uint32_t lvt0 = apic_timer_get_lvt_raw();
        uint32_t init0 = apic_timer_get_initial_count();
        uint32_t cur0 = apic_timer_get_current_count();
        for (volatile int i = 0; i < 5000000; i++) {
            __asm__ volatile ("pause");
        }
        uint64_t t1 = scheduler_get_ticks();
        uint32_t ap1 = apic_timer_get_ticks();
        uint32_t lvt1 = apic_timer_get_lvt_raw();
        uint32_t init1 = apic_timer_get_initial_count();
        uint32_t cur1 = apic_timer_get_current_count();
        bool apic_progress = (ap1 > ap0) || (cur1 != cur0);
        bool sched_progress = (t1 > t0);
        if (!apic_progress && !sched_progress) {
            kputs("[INIT-10.6] APIC timer stalled, fallback to PIT\n");
            kprintf("[INIT-10.6.1] LAPIC diag: sched=%llu->%llu apic_ticks=%u->%u hz=%u\n",
                    (unsigned long long)t0,
                    (unsigned long long)t1,
                    ap0,
                    ap1,
                    apic_timer_get_frequency());
            kprintf("[INIT-10.6.2] LAPIC diag: lvt=%x->%x init=%u->%u cur=%u->%u\n",
                    lvt0, lvt1, init0, init1, cur0, cur1);
            apic_timer_stop();
            if (pit_init(100) == 0) {
                pit_enable();
                g_timer_use_apic = false;
                kputs("[INIT-10.7] PIT fallback active\n");
            } else {
                kputs("[INIT-10.7] PIT fallback failed\n");
            }
        }
    }

    kputs("[INIT-10-OK] Interrupts enabled\n");
    bootlog_mark("interrupts", "enable_done");
    __asm__ volatile ("" ::: "memory");
    
    bool force_kernel_shell = false;
    boot_info_t* boot_cfg = boot_get_info();
    if (boot_cfg) {
        force_kernel_shell =
            bootarg_has_token(boot_cfg->cmdline, "rdnx.shell=1") ||
            bootarg_has_token(boot_cfg->cmdline, "shell=1");
    }
    bootarg_pick_init_path(g_user_init_path, sizeof(g_user_init_path));

    /* Step 11: Bootstrap mode selection */
    kputs("[INIT-11] Bootstrap\n");
    bootlog_mark("shell", "enter");
    __asm__ volatile ("" ::: "memory");
    if (force_kernel_shell) {
        kputs("[INIT-11.1] Kernel shell forced by boot arg (rdnx.shell=1)\n");
    } else {
        kprintf("[INIT-11.1] Userspace init target: %s\n", g_user_init_path);
    }
    bootlog_mark("shell", "done");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-OK] Kernel ready\n");
    bootlog_mark("kmain", "kernel_ready");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-12] Starting scheduler threads...\n");
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
        primary = thread_create(kernel_task, kernel_shell_thread, NULL);
    } else {
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

    kputs("[INIT-12.1] scheduler_start()\n");
    bootlog_mark("scheduler", "start");
    __asm__ volatile ("" ::: "memory");
    scheduler_start();

    /* Should never reach here */
    for (;;) {
        interrupt_wait();
    }
}
