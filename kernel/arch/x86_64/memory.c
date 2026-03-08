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
#include "../../core/boot.h"
#include "../../common/tracev2.h"
#include "../../../include/console.h"
#include "../../../include/debug.h"
#include "types.h"
#include "config.h"
#include "pmm.h"
#include "paging.h"
#include <stddef.h>
#include <stdbool.h>
#include "../../../include/error.h"

static uint64_t memory_oom_pmm = 0;
static uint64_t memory_oom_vmm = 0;
static uint64_t memory_oom_heap = 0;

void memory_oom_inc_pmm(void)
{
    memory_oom_pmm++;
}

void memory_oom_inc_vmm(void)
{
    memory_oom_vmm++;
}

void memory_oom_inc_heap(void)
{
    memory_oom_heap++;
}

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/* VMM functions - TODO: Implement in separate vmm.c */
static void* vmm_alloc_page_impl(uint64_t flags) {
    /* Temporary physmap-backed VMM: return direct-map address. */
    (void)flags;
    extern uint64_t pmm_alloc_page(void);
    uint64_t phys = pmm_alloc_page();
    if (!phys) {
        return NULL;
    }
    return X86_64_PHYS_TO_VIRT(phys);
}

static void vmm_free_page_impl(void* virt) {
    /* Temporary physmap-backed VMM: convert virt back to phys. */
    if (!virt) {
        return;
    }
    extern void pmm_free_page(uint64_t phys);
    pmm_free_page((uint64_t)X86_64_VIRT_TO_PHYS(virt));
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
    extern int pmm_init_from_mmap(uint64_t memory_start, uint64_t memory_end,
                                  void* bitmap_virt, uint64_t bitmap_phys,
                                  const void* mmap_tag, uint32_t mmap_size, uint32_t entry_size);
    extern int paging_init(void);
    extern void kputs(const char* str);
    extern boot_info_t* boot_get_info(void);
    
    kputs("[MEM-1] Start\n");
    tracev2_emit(TR2_CAT_MEMORY, TR2_EV_MEM_INIT_ENTER, 0, 0);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-2] Call paging_init\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize paging first (uses existing page tables from boot.S) */
    if (paging_init() != 0) {
        kputs("[MEM-ERR] paging_init failed\n");
        tracev2_emit(TR2_CAT_MEMORY, TR2_EV_MEM_INIT_FAIL, 1, 0);
        return -1;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-3] paging_init OK\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-4] Setup PMM params\n");
    __asm__ volatile ("" ::: "memory");
    /* For PMM, we need to allocate a bitmap. For now, use a fixed location
     * in low memory that's already identity-mapped.
     */
    #define PMM_MEMORY_START      0x100000 /* 1MB (after boot code) */
    #define PMM_MEMORY_FALLBACK_END 0x4000000 /* 64MB (fallback) */
    #define PMM_BITMAP_MAX_SIZE    0x100000  /* 1MB bitmap cap */
    
    boot_info_t* bi = boot_get_info();
    uint64_t mem_end = PMM_MEMORY_FALLBACK_END;
    if (bi && bi->mem_upper > PMM_MEMORY_START) {
        mem_end = bi->mem_upper;
    }
    /* If no MMAP is available, clamp to a safe fallback to avoid huge bitmaps. */
    /* Clamp memory range to what the fixed bitmap can represent. */
    {
        uint64_t max_pages = (uint64_t)PMM_BITMAP_MAX_SIZE * 8ULL;
        uint64_t max_mem_end = PMM_MEMORY_START + (max_pages * PAGE_SIZE);
        if (mem_end > max_mem_end) {
            mem_end = max_mem_end;
        }
    }
    if (!(bi && bi->mmap_addr && bi->mmap_size && bi->mmap_entry_size)) {
        if (mem_end > PMM_MEMORY_FALLBACK_END) {
            mem_end = PMM_MEMORY_FALLBACK_END;
        }
    }
    
    /*
     * Place PMM bitmap after kernel + initrd in low memory to avoid overlap
     * as kernel image grows.
     */
    extern char kernel_end;
    uint64_t kernel_end_phys = (uint64_t)X86_64_VIRT_TO_PHYS(&kernel_end);
    uint64_t bitmap_phys = (kernel_end_phys + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (bi && bi->initrd_start && bi->initrd_size) {
        uint64_t initrd_end = bi->initrd_start + bi->initrd_size;
        if (initrd_end > bitmap_phys) {
            bitmap_phys = (initrd_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        }
    }

    /* Use higher-half direct mapping for bitmap access. */
    void* bitmap_virt = X86_64_PHYS_TO_VIRT(bitmap_phys);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-5] Call pmm_init\n");
    __asm__ volatile ("" ::: "memory");
    /* Disable interrupts during early PMM init to avoid reentrancy */
    __asm__ volatile ("cli");
    /* Initialize PMM */
    if (bi && bi->mmap_addr && bi->mmap_size && bi->mmap_entry_size) {
        if (pmm_init_from_mmap(PMM_MEMORY_START, mem_end, bitmap_virt,
                               bitmap_phys, bi->mmap_addr,
                               bi->mmap_size, bi->mmap_entry_size) != 0) {
            kputs("[MEM-ERR] pmm_init_from_mmap failed\n");
            tracev2_emit(TR2_CAT_MEMORY, TR2_EV_MEM_INIT_FAIL, 2, 0);
            __asm__ volatile ("sti");
            return -1;
        }
    } else if (pmm_init(PMM_MEMORY_START, mem_end, bitmap_virt) != 0) {
        kputs("[MEM-ERR] pmm_init failed\n");
        tracev2_emit(TR2_CAT_MEMORY, TR2_EV_MEM_INIT_FAIL, 3, 0);
        __asm__ volatile ("sti");
        return -1;
    }
    __asm__ volatile ("sti");

    /* Reserve kernel image before early page-table allocations */
    extern void pmm_reserve_range(uint64_t start, uint64_t end);
    uint64_t kernel_start = 0x100000ULL;
    pmm_reserve_range(kernel_start, kernel_end_phys);

    if (bi && bi->initrd_start && bi->initrd_size) {
        uint64_t initrd_end = bi->initrd_start + bi->initrd_size;
        if (initrd_end > bi->initrd_start) {
            pmm_reserve_range(bi->initrd_start, initrd_end);
        }
    }

    /* Bootstrap higher-half direct map for low memory (64MB, or initrd end) */
    uint64_t physmap_max = 0x4000000ULL;
    if (bi && bi->initrd_start && bi->initrd_size) {
        uint64_t initrd_end = bi->initrd_start + bi->initrd_size;
        if (initrd_end > physmap_max) {
            physmap_max = (initrd_end + 0x1FFFFFULL) & ~0x1FFFFFULL;
        }
    }
    kputs("[MEM-6] Bootstrap physmap\n");
    __asm__ volatile ("" ::: "memory");
    extern int paging_bootstrap_physmap(uint64_t max_phys);
    if (paging_bootstrap_physmap(physmap_max) != 0) {
        kputs("[MEM-ERR] bootstrap physmap failed\n");
        tracev2_emit(TR2_CAT_MEMORY, TR2_EV_MEM_INIT_FAIL, 4, 0);
        return -1;
    }
    __asm__ volatile ("" ::: "memory");
    PANIC_IF(!paging_is_physmap_ready(), "physmap not ready before identity map drop");

    TRACE_EVENT("memory: physmap ready");
    /* Switch VGA console to higher-half direct map */
    console_set_vga_buffer(X86_64_PHYS_TO_VIRT(0xB8000));

    TRACE_EVENT("memory: drop identity map");
    /* Drop low identity map: lower half becomes user-only */
    paging_disable_identity_map();

    /* Initialize simple kernel heap */
    extern int heap_init(size_t initial_pages);
    if (heap_init(16) != 0) {
        kputs("[MEM-ERR] heap_init failed\n");
        tracev2_emit(TR2_CAT_MEMORY, TR2_EV_MEM_INIT_FAIL, 5, 0);
        return -1;
    }

    /* Release init sections back to PMM */
    extern char __init_start;
    extern char __init_end;
    uint64_t init_start = (uint64_t)X86_64_VIRT_TO_PHYS(&__init_start);
    uint64_t init_end = (uint64_t)X86_64_VIRT_TO_PHYS(&__init_end);
    if (init_end > init_start) {
        TRACE_EVENT("memory: release init sections");
        pmm_release_range(init_start, init_end);
    }

    /* Log PMM summary */
    extern uint64_t pmm_get_total_pages(void);
    extern uint64_t pmm_get_free_pages(void);
    extern uint64_t pmm_get_used_pages(void);
    extern void kprintf(const char* fmt, ...);
    kprintf("[MEM] PMM total=%llu free=%llu used=%llu pages\n",
            (unsigned long long)pmm_get_total_pages(),
            (unsigned long long)pmm_get_free_pages(),
            (unsigned long long)pmm_get_used_pages());
    __asm__ volatile ("" ::: "memory");
    
    kputs("[MEM-OK] Done\n");
    tracev2_emit(TR2_CAT_MEMORY, TR2_EV_MEM_INIT_DONE,
                 pmm_get_free_pages(), pmm_get_used_pages());
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
    (void)flags;
    extern uint64_t pmm_alloc_pages(uint32_t pages);
    uint64_t phys = pmm_alloc_pages(count);
    if (!phys) {
        TRACE_EVENT("oom: vmm_alloc_pages");
        memory_oom_inc_vmm();
        return NULL;
    }
    return X86_64_PHYS_TO_VIRT(phys);
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
    if (!virt || count == 0) {
        return;
    }
    extern void pmm_free_pages(uint64_t phys, uint32_t pages);
    pmm_free_pages((uint64_t)X86_64_VIRT_TO_PHYS(virt), count);
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
        return RDNX_E_INVALID;
    }
    
    /* Get PMM statistics */
    info->total_physical = pmm_get_total_pages() * PAGE_SIZE;
    info->free_physical = pmm_get_free_pages() * PAGE_SIZE;
    info->used_physical = pmm_get_used_pages() * PAGE_SIZE;
    
    /* TODO: Get VMM statistics */
    info->total_virtual = 0;
    info->free_virtual = 0;
    info->used_virtual = 0;
    info->oom_pmm = memory_oom_pmm;
    info->oom_vmm = memory_oom_vmm;
    info->oom_heap = memory_oom_heap;
    
    return RDNX_OK;
}
