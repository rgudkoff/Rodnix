/**
 * @file x86_64/boot.c
 * @brief Boot implementation for x86_64
 * 
 * @note This implementation follows boot argument handling.
 */

#include "../../core/boot.h"
#include "types.h"
#include "config.h"
#include <stddef.h>

/* Multiboot2 tag structure */
struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
    /* Variable data follows */
} __attribute__((packed));

/* Multiboot2 command line tag (type 1) */
struct multiboot2_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
} __attribute__((packed));

/* Use explicit initialization to ensure proper memory layout */
/* Use volatile to prevent compiler optimizations that might cause issues */
static volatile boot_info_t boot_info_storage = {0};
static volatile bool boot_info_valid = false;

int boot_early_init(boot_info_t* info)
{
    if (!info) {
        return -1;
    }
    
    /* Copy boot information (fixed buffer for cmdline) */
    /* Use memory barriers to ensure proper ordering */
    boot_info_storage.magic = info->magic;
    __asm__ volatile ("" ::: "memory");
    
    boot_info_storage.boot_info = info->boot_info;
    __asm__ volatile ("" ::: "memory");
    
    boot_info_storage.mem_lower = info->mem_lower;
    __asm__ volatile ("" ::: "memory");
    
    boot_info_storage.mem_upper = info->mem_upper;
    __asm__ volatile ("" ::: "memory");
    
    boot_info_storage.flags = info->flags;
    __asm__ volatile ("" ::: "memory");
    
    /* Initialize cmdline buffer to empty string (fixed buffer) */
    /* TODO: Parse cmdline from Multiboot2 info later when memory is fully initialized */
    boot_info_storage.cmdline[0] = '\0';
    __asm__ volatile ("" ::: "memory");
    
    /* Set valid flag last, with memory barrier */
    boot_info_valid = true;
    __asm__ volatile ("" ::: "memory");
    
    return 0;
}

int boot_arch_init(void)
{
    /* Initialize architecture-dependent components for x86_64 */
    /* GDT, IDT, etc. are already initialized in boot.S */
    
    return 0;
}

int boot_switch_to_64bit(void)
{
    /* Switch to 64-bit mode already done in boot.S */
    /* This function is called for compatibility but does nothing */
    
    return 0;
}

int boot_memory_init(boot_info_t* info)
{
    if (!info) {
        if (!boot_info_valid) {
            return -1;
        }
        info = &boot_info_storage;
    }
    
    /* Early memory initialization */
    /* PMM is already initialized */
    
    return 0;
}

int boot_interrupts_init(void)
{
    /* Early interrupt initialization */
    /* IDT is already initialized */
    
    return 0;
}

boot_info_t* boot_get_info(void)
{
    /* Use volatile read to prevent optimization issues */
    volatile bool valid = boot_info_valid;
    __asm__ volatile ("" ::: "memory");
    
    if (!valid) {
        return NULL;
    }
    
    /* Cast away volatile for return (caller knows it's safe) */
    return (boot_info_t*)&boot_info_storage;
}

