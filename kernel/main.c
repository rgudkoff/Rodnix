/**
 * @file main.c
 * @brief Kernel main entry point
 */

#include "../include/kernel.h"
#include "../include/console.h"
#include "../include/debug.h"

static void idle_thread(void* arg)
{
    (void)arg;
    for (;;) {
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
    /* Initialize console first - this is critical for debugging */
    console_init();
    console_clear();
    
    /* Print welcome message */
    kputs("========================================\n");
    kputs("    RodNIX Kernel v0.1\n");
    kputs("    64-bit Architecture\n");
    kputs("========================================\n\n");
    
    kputs("[DEBUG] kmain: Entry point reached\n");
    kputs("[DEBUG] kmain: magic = ");
    extern void kprint_hex(uint64_t num);
    kprint_hex(magic);
    kputs(", mbi = ");
    kprint_hex((uint64_t)mbi);
    kputs("\n");
    
    kputs("[INIT] Starting kernel...\n");
    __asm__ volatile ("" ::: "memory");
    
    /* Step 1: Initialize boot subsystem */
    kputs("[INIT-1] Boot subsystem\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.1] Create boot_info struct\n");
    __asm__ volatile ("" ::: "memory");
    boot_info_t boot_info;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.2] Fill boot_info\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.2.1] magic\n");
    __asm__ volatile ("" ::: "memory");
    boot_info.magic = magic;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.2.2] boot_info\n");
    __asm__ volatile ("" ::: "memory");
    boot_info.boot_info = mbi;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.2.3] mem_lower\n");
    __asm__ volatile ("" ::: "memory");
    boot_info.mem_lower = 0;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.2.4] mem_upper\n");
    __asm__ volatile ("" ::: "memory");
    boot_info.mem_upper = 0;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.2.5] cmdline\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize cmdline buffer to empty string (fixed buffer) */
    boot_info.cmdline[0] = '\0';
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.2.6] flags\n");
    __asm__ volatile ("" ::: "memory");
    boot_info.flags = 0;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.3] Call boot_early_init\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.3.1] Before call\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.3.2] Calling function\n");
    __asm__ volatile ("" ::: "memory");
    int boot_result = boot_early_init(&boot_info);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.3.3] After call\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1.3.4] Check result\n");
    __asm__ volatile ("" ::: "memory");
    if (boot_result != 0) {
        kputs("[INIT-1-ERR] Boot init failed\n");
        panic("Boot init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-1-OK] Boot done\n");
    __asm__ volatile ("" ::: "memory");
    
    /* Step 2: Initialize CPU */
    kputs("[INIT-2] CPU\n");
    __asm__ volatile ("" ::: "memory");
    extern int cpu_init(void);
    if (cpu_init() != 0) {
        panic("CPU init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 3: Initialize interrupts */
    kputs("[INIT-3] Interrupts\n");
    __asm__ volatile ("" ::: "memory");
    if (interrupts_init() != 0) {
        panic("Interrupts init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 4: Initialize memory */
    kputs("[INIT-4] Memory\n");
    __asm__ volatile ("" ::: "memory");
    if (memory_init() != 0) {
        panic("Memory init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 5: Initialize APIC (after paging is ready) */
    kputs("[INIT-5] APIC\n");
    __asm__ volatile ("" ::: "memory");
    extern int apic_init(void);
    extern bool apic_is_available(void);
    if (apic_init() == 0) {
        kputs("[INIT-5.1] APIC initialized\n");
    } else {
        kputs("[INIT-5.2] APIC init failed, fallback to PIC\n");
    }
    __asm__ volatile ("" ::: "memory");

    /* If LAPIC is available, PIC should be disabled */
    /* But if I/O APIC is not available, keep PIC for external IRQ routing */
    if (apic_is_available()) {
        extern bool ioapic_is_available(void);
        extern void pic_disable(void);
        if (ioapic_is_available()) {
            kputs("[INIT-5.3] I/O APIC available, disable PIC completely\n");
            __asm__ volatile ("" ::: "memory");
            pic_disable();
            __asm__ volatile ("" ::: "memory");
        } else {
            kputs("[INIT-5.3] LAPIC available, I/O APIC not - keep PIC for external IRQ\n");
            __asm__ volatile ("" ::: "memory");
        }
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 6: Initialize timer (LAPIC timer if available, otherwise PIT) */
    kputs("[INIT-6] Timer\n");
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
            panic("Timer init failed");
        }
    }
    __asm__ volatile ("" ::: "memory");

    if (use_apic_timer) {
        kputs("[INIT-6.9] Timer source: LAPIC\n");
    } else {
        kputs("[INIT-6.9] Timer source: PIT\n");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 7: Initialize scheduler */
    kputs("[INIT-7] Scheduler\n");
    __asm__ volatile ("" ::: "memory");
    if (scheduler_init() != 0) {
        panic("Scheduler init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 8: Initialize IPC */
    kputs("[INIT-8] IPC\n");
    __asm__ volatile ("" ::: "memory");
    if (ipc_init() != 0) {
        panic("IPC init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 9: Initialize Fabric */
    kputs("[INIT-9] Fabric\n");
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
    __asm__ volatile ("" ::: "memory");
    
    /* Step 10: Enable interrupts (set IRQL to PASSIVE) */
    kputs("[INIT-10] Enable interrupts\n");
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
    __asm__ volatile ("" ::: "memory");
    
    /* Step 11: Initialize shell */
    kputs("[INIT-11] Shell\n");
    __asm__ volatile ("" ::: "memory");
    extern int shell_init(void);
    if (shell_init() != 0) {
        panic("Shell init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-OK] Kernel ready\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-12] Starting scheduler threads...\n");
    __asm__ volatile ("" ::: "memory");

    task_t* kernel_task = task_create();
    if (!kernel_task) {
        panic("Kernel task create failed");
    }
    kernel_task->state = TASK_STATE_READY;

    thread_t* shell = thread_create(kernel_task, shell_thread, NULL);
    thread_t* idle = thread_create(kernel_task, idle_thread, NULL);
    if (!shell || !idle) {
        panic("Kernel thread create failed");
    }

    scheduler_add_thread(shell);
    scheduler_add_thread(idle);

    kputs("[INIT-12.1] scheduler_start()\n");
    __asm__ volatile ("" ::: "memory");
    scheduler_start();

    /* Should never reach here */
    for (;;) {
        interrupt_wait();
    }
}
