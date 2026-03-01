/**
 * @file main.c
 * @brief Kernel main entry point
 */

#include "../include/kernel.h"
#include "../include/console.h"
#include "../include/debug.h"
#include "core/interrupts.h"
#include "syscall.h"
#include "fs/vfs.h"
#include "net/net.h"
#include "common/security.h"
#include "common/bootstrap.h"
#include "common/loader.h"
#include "common/bootlog.h"
#include "common/idl_demo.h"
#include "core/boot.h"
#include "arch/x86_64/config.h"

static void idle_thread(void* arg)
{
    (void)arg;
    for (;;) {
        interrupts_enable();
        /* Yield if any ready thread exists */
        scheduler_yield();
        cpu_idle();
    }
}

static void shell_thread(void* arg)
{
    (void)arg;
    extern void shell_run(void);
    shell_run();
    for (;;) {
        cpu_idle();
    }
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
    kputs("    RodNIX Kernel v0.1\n");
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
    bootlog_init();
    bootlog_mark("boot", "done");
    kputs("[INIT] Boot done\n");
    
    /* Step 2: Initialize CPU */
    kputs("[INIT] CPU\n");
    bootlog_mark("cpu", "enter");
    extern int cpu_init(void);
    if (cpu_init() != 0) {
        bootlog_mark("cpu", "fail");
        panic("CPU init failed");
    }
    bootlog_mark("cpu", "done");
    
    /* Step 3: Initialize interrupts */
    kputs("[INIT] Interrupts\n");
    bootlog_mark("interrupts", "enter");
    if (interrupts_init() != 0) {
        bootlog_mark("interrupts", "fail");
        panic("Interrupts init failed");
    }
    bootlog_mark("interrupts", "done");
    
    /* Step 4: Initialize memory */
    kputs("[INIT] Memory\n");
    bootlog_mark("memory", "enter");
    if (memory_init() != 0) {
        bootlog_mark("memory", "fail");
        panic("Memory init failed");
    }
    bootlog_mark("memory", "done");
    
    /* Step 5: Initialize APIC (after paging is ready) */
    kputs("[INIT-5] APIC\n");
    bootlog_mark("apic", "enter");
    __asm__ volatile ("" ::: "memory");
    extern int apic_init(void);
    extern bool apic_is_available(void);
    if (apic_init() == 0) {
        bootlog_mark("apic", "done");
        kputs("[INIT-5.1] APIC initialized\n");
    } else {
        bootlog_mark("apic", "fallback_pic");
        kputs("[INIT-5.2] APIC init failed, fallback to PIC\n");
    }
    __asm__ volatile ("" ::: "memory");

    /* If LAPIC is available, keep PIC enabled for legacy IRQs (PS/2) */
    if (apic_is_available()) {
        extern bool ioapic_is_available(void);
        if (ioapic_is_available()) {
            kputs("[INIT-5.3] I/O APIC available, keep PIC enabled for legacy IRQs\n");
            kputs("[DEGRADED] IRQ routing is mixed (IOAPIC + PIC legacy IRQs)\n");
            __asm__ volatile ("" ::: "memory");
        } else {
            kputs("[INIT-5.3] LAPIC available, I/O APIC not - keep PIC for external IRQ\n");
            kputs("[DEGRADED] External IRQ routing stays on PIC fallback path\n");
            __asm__ volatile ("" ::: "memory");
        }
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 6: Initialize timer (LAPIC timer if available, otherwise PIT) */
    kputs("[INIT-6] Timer\n");
    bootlog_mark("timer", "enter");
    __asm__ volatile ("" ::: "memory");
    extern int apic_timer_init(uint32_t frequency);
    extern int pit_init(uint32_t frequency);
    
    bool use_apic_timer = false;
    if (apic_is_available()) {
        kputs("[INIT-6.1] Use LAPIC timer\n");
        __asm__ volatile ("" ::: "memory");
        if (apic_timer_init(100) == 0) {
            use_apic_timer = true;
        } else {
            kputs("[INIT-6.1.1] LAPIC timer failed, fallback to PIT\n");
            __asm__ volatile ("" ::: "memory");
        }
    }
    
    if (!use_apic_timer) {
        kputs("[INIT-6.2] Use PIT\n");
        __asm__ volatile ("" ::: "memory");
        if (pit_init(100) != 0) {
            bootlog_mark("timer", "fail");
            panic("Timer init failed");
        }
    }
    __asm__ volatile ("" ::: "memory");

    if (use_apic_timer) {
        bootlog_mark("timer", "lapic");
        kputs("[INIT-6.9] Timer source: LAPIC\n");
    } else {
        bootlog_mark("timer", "pit");
        kputs("[INIT-6.9] Timer source: PIT\n");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 7: Initialize scheduler */
    kputs("[INIT-7] Scheduler\n");
    bootlog_mark("scheduler", "enter");
    __asm__ volatile ("" ::: "memory");
    if (scheduler_init() != 0) {
        bootlog_mark("scheduler", "fail");
        panic("Scheduler init failed");
    }
    bootlog_mark("scheduler", "done");
    __asm__ volatile ("" ::: "memory");
    
    /* Step 8: Initialize IPC */
    kputs("[INIT-8] IPC\n");
    bootlog_mark("ipc", "enter");
    __asm__ volatile ("" ::: "memory");
    if (ipc_init() != 0) {
        bootlog_mark("ipc", "fail");
        panic("IPC init failed");
    }
    bootlog_mark("ipc", "done");
    __asm__ volatile ("" ::: "memory");

    /* Step 8.5: Initialize syscalls */
    kputs("[INIT-8.5] Syscalls\n");
    bootlog_mark("syscall", "enter");
    __asm__ volatile ("" ::: "memory");
    syscall_init();
    bootlog_mark("syscall", "done");
    __asm__ volatile ("" ::: "memory");

    kputs("[INIT-8.6] Security\n");
    bootlog_mark("security", "enter");
    __asm__ volatile ("" ::: "memory");
    security_init();
    bootlog_mark("security", "done");
    __asm__ volatile ("" ::: "memory");

    kputs("[INIT-8.7] Loader\n");
    bootlog_mark("loader", "enter");
    __asm__ volatile ("" ::: "memory");
    loader_init();
    bootlog_mark("loader", "done");
    __asm__ volatile ("" ::: "memory");
    
    /* Step 9: Initialize Fabric */
    kputs("[INIT-9] Fabric\n");
    bootlog_mark("fabric", "enter");
    __asm__ volatile ("" ::: "memory");
    extern void fabric_init(void);
    extern void virt_bus_init(void);
    extern void pci_bus_init(void);
    extern void ps2_bus_init(void);
    extern void hid_kbd_init(void);
    
    fabric_init();
    kputs("[INIT-9.1] Fabric initialized\n");
    __asm__ volatile ("" ::: "memory");
    
    virt_bus_init();
    kputs("[INIT-9.2] Virt bus initialized\n");
    __asm__ volatile ("" ::: "memory");
    
    pci_bus_init();
    kputs("[INIT-9.3] PCI bus initialized\n");
    __asm__ volatile ("" ::: "memory");
    
    ps2_bus_init();  /* PS/2 bus for keyboard */
    kputs("[INIT-9.4] PS/2 bus initialized\n");
    __asm__ volatile ("" ::: "memory");
    
    hid_kbd_init();  /* HID keyboard driver */
    kputs("[INIT-9.5] HID keyboard driver initialized\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-9-OK] Fabric initialization complete\n");
    bootlog_mark("fabric", "done");
    __asm__ volatile ("" ::: "memory");

    kputs("[INIT-9.6] VFS/RAMFS\n");
    bootlog_mark("vfs", "enter");
    __asm__ volatile ("" ::: "memory");
    boot_info_t* bi = boot_get_info();
    if (bi && bi->initrd_start && bi->initrd_size) {
        kprintf("[INIT-9.6] initrd: start=%llx size=%llu\n",
                (unsigned long long)bi->initrd_start,
                (unsigned long long)bi->initrd_size);
        void* initrd = X86_64_PHYS_TO_VIRT(bi->initrd_start);
        vfs_set_initrd(initrd, (size_t)bi->initrd_size);
    }
    if (vfs_init() != 0) {
        bootlog_mark("vfs", "fail");
        kputs("[INIT-9.6] VFS init failed\n");
    } else {
        bootlog_mark("vfs", "done");
        kputs("[INIT-9.6] VFS ready\n");
    }
    __asm__ volatile ("" ::: "memory");

    kputs("[INIT-9.7] Network\n");
    bootlog_mark("net", "enter");
    __asm__ volatile ("" ::: "memory");
    if (net_init() != 0) {
        bootlog_mark("net", "fail");
        kputs("[INIT-9.7] Net init failed\n");
    } else {
        bootlog_mark("net", "done");
        kputs("[INIT-9.7] Net ready (stub)\n");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 10: Enable interrupts (set IRQL to PASSIVE) */
    kputs("[INIT-10] Enable interrupts\n");
    bootlog_mark("interrupts", "enable_enter");
    __asm__ volatile ("" ::: "memory");
    
    /* Temporarily disable timer to avoid immediate interrupt on sti */
    kputs("[INIT-10.1] Disable timer\n");
    __asm__ volatile ("" ::: "memory");
    extern bool apic_is_available(void);
    extern void apic_timer_stop(void);
    extern void pit_disable(void);
    if (apic_is_available()) {
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
    extern bool apic_is_available(void);
    extern void apic_timer_start(void);
    extern void pit_enable(void);
    if (apic_is_available()) {
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
    
    kputs("[INIT-10-OK] Interrupts enabled\n");
    bootlog_mark("interrupts", "enable_done");
    __asm__ volatile ("" ::: "memory");
    
    /* Step 11: Initialize shell */
    kputs("[INIT-11] Shell\n");
    bootlog_mark("shell", "enter");
    __asm__ volatile ("" ::: "memory");
    extern int shell_init(void);
    if (shell_init() != 0) {
        bootlog_mark("shell", "fail");
        panic("Shell init failed");
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

    thread_t* shell = thread_create(kernel_task, shell_thread, NULL);
    thread_t* idle = thread_create(kernel_task, idle_thread, NULL);
    bootstrap_start();
    idl_demo_start();
    if (!shell || !idle) {
        bootlog_mark("threads", "thread_create_fail");
        panic("Kernel thread create failed");
    }

    scheduler_add_thread(shell);
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
