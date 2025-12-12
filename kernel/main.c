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
    (void)magic;
    (void)mbi;
    
    /* Initialize console */
    console_init();
    console_clear();
    
    /* Print welcome message */
    kputs("========================================\n");
    kputs("    RodNIX Kernel v0.1\n");
    kputs("    64-bit Architecture\n");
    kputs("========================================\n\n");
    
    DEBUG_INFO("Kernel starting...");
    
    /* Initialize boot subsystem */
    boot_info_t boot_info = {
        .magic = magic,
        .boot_info = mbi,
        .mem_lower = 0,
        .mem_upper = 0,
        .cmdline = NULL,
        .flags = 0
    };
    
    if (boot_early_init(&boot_info) != 0) {
        panic("Failed to initialize boot subsystem");
    }
    
    DEBUG_INFO("Boot subsystem initialized");
    
    /* Initialize CPU */
    if (cpu_init() != 0) {
        panic("Failed to initialize CPU");
    }
    
    DEBUG_INFO("CPU initialized");
    
    /* Initialize interrupts */
    if (interrupts_init() != 0) {
        panic("Failed to initialize interrupts");
    }
    
    DEBUG_INFO("Interrupts initialized");
        
    /* Initialize memory */
    if (memory_init() != 0) {
        panic("Failed to initialize memory");
        }
        
    DEBUG_INFO("Memory initialized");
    
    /* Initialize scheduler */
    if (scheduler_init() != 0) {
        panic("Failed to initialize scheduler");
        }
        
    DEBUG_INFO("Scheduler initialized");
    
    /* Initialize IPC */
    if (ipc_init() != 0) {
        panic("Failed to initialize IPC");
    }
    
    DEBUG_INFO("IPC initialized");
            
    /* Initialize device manager */
    if (device_manager_init() != 0) {
        panic("Failed to initialize device manager");
    }
    
    DEBUG_INFO("Device manager initialized");
    
    kputs("\nKernel initialized successfully!\n");
    kputs("Entering main loop...\n\n");
    
    /* Main loop */
    for (;;) {
        /* Wait for interrupt */
        interrupt_wait();
        
        /* TODO: Handle events, schedule tasks, etc. */
        }
    }
