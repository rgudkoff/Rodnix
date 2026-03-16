/**
 * @file main.c
 * @brief Kernel main entry point
 */

#include "../include/kernel.h"
#include "../include/console.h"
#include "../include/debug.h"
#include "../include/version.h"
#include "common/bootlog.h"
#include "common/startup_trace.h"
#include "core/boot.h"
#include "init/init.h"

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

    kernel_run_bootstrap_sysinit();
    kernel_enable_runtime_interrupts();
    kernel_enter_runtime();
}
