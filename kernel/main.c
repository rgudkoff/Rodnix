/**
 * @file main.c
 * @brief Kernel main entry point
 */

#include "../include/kernel.h"
#include "../include/console.h"
#include "../include/debug.h"

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
    /* Initialize cmdline buffer to empty string (XNU-style: fixed buffer) */
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
    
    /* Step 4: Initialize timer (PIT) */
    kputs("[INIT-4] Timer (PIT)\n");
    __asm__ volatile ("" ::: "memory");
    extern int pit_init(uint32_t frequency);
    if (pit_init(100) != 0) {
        panic("PIT init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 5: Initialize memory */
    kputs("[INIT-5] Memory\n");
    __asm__ volatile ("" ::: "memory");
    if (memory_init() != 0) {
        panic("Memory init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 6: Initialize scheduler */
    kputs("[INIT-6] Scheduler\n");
    __asm__ volatile ("" ::: "memory");
    if (scheduler_init() != 0) {
        panic("Scheduler init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 7: Initialize IPC */
    kputs("[INIT-7] IPC\n");
    __asm__ volatile ("" ::: "memory");
    if (ipc_init() != 0) {
        panic("IPC init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 8: Initialize device manager */
    kputs("[INIT-8] Device manager\n");
    __asm__ volatile ("" ::: "memory");
    if (device_manager_init() != 0) {
        panic("Device manager init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 9: Initialize keyboard */
    kputs("[INIT-9] Keyboard\n");
    __asm__ volatile ("" ::: "memory");
    extern int keyboard_init(void);
    if (keyboard_init() != 0) {
        panic("Keyboard init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 10: Enable interrupts */
    kputs("[INIT-10] Enable interrupts\n");
    __asm__ volatile ("" ::: "memory");
    extern void interrupts_enable(void);
    interrupts_enable();
    __asm__ volatile ("" ::: "memory");
    
    /* Step 11: Initialize shell */
    kputs("[INIT-11] Shell\n");
    __asm__ volatile ("" ::: "memory");
    extern int shell_init(void);
    extern void shell_run(void);
    if (shell_init() != 0) {
        panic("Shell init failed");
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-OK] Kernel ready\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[INIT-12] Starting shell...\n");
    __asm__ volatile ("" ::: "memory");
    
    /* Run shell (blocks) */
    shell_run();
    
    /* Should never reach here */
    for (;;) {
        interrupt_wait();
    }
    }
