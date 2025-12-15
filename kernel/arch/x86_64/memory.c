/**
 * @file memory.c
 * @brief x86_64 memory management implementation
 * 
 * This module provides the architecture-specific implementation of memory
 * management for x86_64, including page mapping and virtual memory management.
 * 
 * @note This implementation is adapted for RodNIX.
 */

#include "../../core/memory.h"
#include "types.h"
#include "config.h"
#include "pmm.h"
#include "paging.h"
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/* VMM functions - TODO: Implement in separate vmm.c */
static void* vmm_alloc_page_impl(uint64_t flags) {
    /* TODO: Implement actual VMM allocation */
    (void)flags;
    return NULL;
}

static void vmm_free_page_impl(void* virt) {
    /* TODO: Implement actual VMM deallocation */
    (void)virt;
}

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function memory_init
 * @brief Initialize memory subsystem
 * 
 * This function initializes both PMM (Physical Memory Manager) and paging.
 * It must be called after interrupts are initialized (for PMM allocation).
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note PMM initialization requires a bitmap area, which we allocate from
 *       early memory. For simplicity, we use a fixed location.
 */
int memory_init(void)
{
    extern int pmm_init(uint64_t memory_start, uint64_t memory_end, void* bitmap_virt);
    extern int paging_init(void);
    extern void kputs(const char* str);
    
    kputs("[MEM-1] Start\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-2] Call paging_init\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize paging first (uses existing page tables from boot.S) */
    if (paging_init() != 0) {
        kputs("[MEM-ERR] paging_init failed\n");
        return -1;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-3] paging_init OK\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-4] Setup PMM params\n");
    __asm__ volatile ("" ::: "memory");
    /* For PMM, we need to allocate a bitmap. For now, use a simple approach:
     * Use a fixed location in low memory that's already identity-mapped.
     * In a real system, we'd parse Multiboot2 memory map and allocate properly.
     */
    #define PMM_BITMAP_PHYS_ADDR  0x50000                 /* 320KB (after boot code, before 1MB) */
    #define PMM_MEMORY_START      0x100000                /* 1MB (after boot code) */
    #define PMM_MEMORY_END        0x4000000               /* 64MB (conservative estimate) */
    
    /* Bitmap is in low memory, already identity-mapped by boot.S */
    void* bitmap_virt = (void*)PMM_BITMAP_PHYS_ADDR;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-5] Call pmm_init\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize PMM */
    if (pmm_init(PMM_MEMORY_START, PMM_MEMORY_END, bitmap_virt) != 0) {
        kputs("[MEM-ERR] pmm_init failed\n");
        return -1;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-OK] Done\n");
    return 0;
}

/* paging_init is implemented in paging.c */

/**
 * @function page_map
 * @brief Map a virtual page to a physical page
 * 
 * @param virt Virtual address (must be page-aligned)
 * @param phys Physical address (must be page-aligned)
 * @param flags Page flags (PAGE_FLAG_*)
 * @param type Page type (PAGE_TYPE_*)
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note This function converts architecture-independent flags to x86_64
 *       specific flags and calls the appropriate paging function.
 */
int page_map(uint64_t virt, uint64_t phys, uint64_t flags, page_type_t type)
{
    uint64_t x86_flags = 0;
    
    /* Convert architecture-independent flags to x86_64 flags */
    if (flags & PAGE_FLAG_PRESENT)  x86_flags |= PTE_PRESENT;
    if (flags & PAGE_FLAG_WRITABLE) x86_flags |= PTE_RW;
    if (flags & PAGE_FLAG_USER)     x86_flags |= PTE_USER;
    if (flags & PAGE_FLAG_NOCACHE)  x86_flags |= PTE_PCD;
    if (flags & PAGE_FLAG_GLOBAL)   x86_flags |= PTE_GLOBAL;
    if (!(flags & PAGE_FLAG_EXECUTE)) {
        x86_flags |= PTE_NX; /* NX (No Execute) bit */
    }
    
    /* Map based on page type */
    if (type == PAGE_TYPE_2MB) {
        return paging_map_page_2mb(virt, phys, x86_flags);
    } else if (type == PAGE_TYPE_1GB) {
        /* TODO: Implement 1GB page mapping */
        /* For now, fall back to 2MB or 4KB */
        return paging_map_page_2mb(virt, phys, x86_flags);
    } else {
        /* Default: 4KB page */
        return paging_map_page_4kb(virt, phys, x86_flags);
    }
}

/**
 * @function page_unmap
 * @brief Unmap a virtual page
 * 
 * @param virt Virtual address (must be page-aligned)
 * 
 * @return 0 on success, -1 on failure
 */
int page_unmap(uint64_t virt)
{
    return paging_unmap_page(virt);
}

/**
 * @function page_get_physical
 * @brief Get physical address for a virtual address
 * 
 * @param virt Virtual address
 * 
 * @return Physical address, or 0 on failure
 */
uint64_t page_get_physical(uint64_t virt)
{
    return paging_get_physical(virt);
}

/**
 * @function page_get_virtual
 * @brief Get virtual address for a physical address (if identity mapped)
 * 
 * @param phys Physical address
 * 
 * @return Virtual address, or 0 if not mapped
 */
uint64_t page_get_virtual(uint64_t phys)
{
    /* For x86_64 with identity mapping */
    if (phys < 0x400000) {
        return phys;  /* Identity mapped low memory */
    }
    /* Or through high-half mapping */
    return phys + X86_64_KERNEL_VIRT_BASE;
}

/**
 * @function vmm_alloc_page
 * @brief Allocate a virtual page
 * 
 * @param flags Page flags
 * 
 * @return Virtual address, or NULL on failure
 */
void* vmm_alloc_page(uint64_t flags)
{
    return vmm_alloc_page_impl(flags);
}

/**
 * @function vmm_free_page
 * @brief Free a virtual page
 * 
 * @param virt Virtual address
 */
void vmm_free_page(void* virt)
{
    vmm_free_page_impl(virt);
}

/**
 * @function vmm_alloc_pages
 * @brief Allocate multiple virtual pages
 * 
 * @param count Number of pages
 * @param flags Page flags
 * 
 * @return Virtual address of first page, or NULL on failure
 */
void* vmm_alloc_pages(uint32_t count, uint64_t flags)
{
    void* first_page = vmm_alloc_page(flags);
    if (!first_page) {
        return NULL;
    }
    
    for (uint32_t i = 1; i < count; i++) {
        void* page = vmm_alloc_page(flags);
        if (!page) {
            /* Free already allocated pages */
            for (uint32_t j = 0; j < i; j++) {
                vmm_free_page((void*)((uintptr_t)first_page + j * PAGE_SIZE));
            }
            return NULL;
        }
    }
    
    return first_page;
}

/**
 * @function vmm_free_pages
 * @brief Free multiple virtual pages
 * 
 * @param virt Virtual address of first page
 * @param count Number of pages
 */
void vmm_free_pages(void* virt, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        vmm_free_page((void*)((uintptr_t)virt + i * PAGE_SIZE));
    }
}

/**
 * @function memory_get_info
 * @brief Get memory statistics
 * 
 * @param info Pointer to memory_info_t structure to fill
 * 
 * @return 0 on success, -1 on failure
 */
int memory_get_info(memory_info_t* info)
{
    if (!info) {
        return -1;
    }
    
    /* Get PMM statistics */
    info->total_physical = pmm_get_total_pages() * PAGE_SIZE;
    info->free_physical = pmm_get_free_pages() * PAGE_SIZE;
    info->used_physical = pmm_get_used_pages() * PAGE_SIZE;
    
    /* TODO: Get VMM statistics */
    info->total_virtual = 0;
    info->free_virtual = 0;
    info->used_virtual = 0;
    
    return 0;
}
