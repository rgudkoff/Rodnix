/**
 * @file pmm.c
 * @brief Physical Memory Manager (PMM) implementation for x86_64
 * 
 * This module implements physical memory management using a bitmap-based
 * allocator. It tracks free and used physical pages and provides allocation
 * and deallocation functions.
 * 
 * @note This implementation follows XNU-style architecture but is adapted for RodNIX.
 */

#include "types.h"
#include "config.h"
#include "../../include/debug.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * PMM Constants
 * ============================================================================ */

/* Memory regions */
#define PMM_MEMORY_START     0x100000    /* Start of usable memory (after 1MB) */
#define PMM_MEMORY_END       0xFFFFFFFFFFFFF000ULL  /* Maximum 64-bit physical address (4KB aligned) */

/* Bitmap constants */
#define BITS_PER_BYTE        8
#define BITS_PER_WORD        32
#define WORDS_PER_PAGE       (PAGE_SIZE / sizeof(uint32_t))

/* ============================================================================
 * PMM State
 * ============================================================================ */

/**
 * @struct pmm_state
 * @brief PMM internal state
 */
struct pmm_state {
    uint64_t total_pages;        /* Total number of physical pages */
    uint64_t free_pages;         /* Number of free pages */
    uint64_t used_pages;         /* Number of used pages */
    uint64_t bitmap_start;       /* Physical address of bitmap start */
    uint64_t bitmap_size;        /* Size of bitmap in bytes */
    uint32_t* bitmap;            /* Pointer to bitmap (virtual address) */
    uint64_t memory_start;       /* Start of managed memory */
    uint64_t memory_end;         /* End of managed memory */
};

/* Global PMM state */
static struct pmm_state pmm_state = {0};

/* ============================================================================
 * Bitmap Operations
 * ============================================================================ */

/**
 * @function pmm_bitmap_set
 * @brief Set a bit in the bitmap (mark page as used)
 * 
 * @param page_index Page index (0-based)
 */
static void pmm_bitmap_set(uint64_t page_index)
{
    uint64_t word_index = page_index / BITS_PER_WORD;
    uint32_t bit_index = page_index % BITS_PER_WORD;
    
    if (word_index < (pmm_state.bitmap_size / sizeof(uint32_t))) {
        pmm_state.bitmap[word_index] |= (1U << bit_index);
    }
}

/**
 * @function pmm_bitmap_clear
 * @brief Clear a bit in the bitmap (mark page as free)
 * 
 * @param page_index Page index (0-based)
 */
static void pmm_bitmap_clear(uint64_t page_index)
{
    uint64_t word_index = page_index / BITS_PER_WORD;
    uint32_t bit_index = page_index % BITS_PER_WORD;
    
    if (word_index < (pmm_state.bitmap_size / sizeof(uint32_t))) {
        pmm_state.bitmap[word_index] &= ~(1U << bit_index);
    }
}

/**
 * @function pmm_bitmap_test
 * @brief Test if a bit is set in the bitmap
 * 
 * @param page_index Page index (0-based)
 * @return true if page is used, false if free
 */
static bool pmm_bitmap_test(uint64_t page_index)
{
    uint64_t word_index = page_index / BITS_PER_WORD;
    uint32_t bit_index = page_index % BITS_PER_WORD;
    
    if (word_index < (pmm_state.bitmap_size / sizeof(uint32_t))) {
        return (pmm_state.bitmap[word_index] & (1U << bit_index)) != 0;
    }
    return true; /* Out of range, consider as used */
}

/**
 * @function pmm_page_to_index
 * @brief Convert physical address to page index
 * 
 * @param phys Physical address
 * @return Page index
 */
static uint64_t pmm_page_to_index(uint64_t phys)
{
    if (phys < pmm_state.memory_start) {
        return 0;
    }
    return (phys - pmm_state.memory_start) / PAGE_SIZE;
}

/**
 * @function pmm_index_to_page
 * @brief Convert page index to physical address
 * 
 * @param index Page index
 * @return Physical address
 */
static uint64_t pmm_index_to_page(uint64_t index)
{
    return pmm_state.memory_start + (index * PAGE_SIZE);
}

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function pmm_init
 * @brief Initialize the Physical Memory Manager
 * 
 * This function initializes the PMM with the given memory range.
 * It sets up the bitmap and marks all pages as free initially.
 * 
 * @param memory_start Start of managed memory (physical address)
 * @param memory_end End of managed memory (physical address)
 * @param bitmap_virt Virtual address of bitmap storage
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note The bitmap must be allocated and mapped before calling this function.
 * @note All pages in the range are initially marked as free.
 */
int pmm_init(uint64_t memory_start, uint64_t memory_end, void* bitmap_virt)
{
    extern void kputs(const char* str);
    
    kputs("[PMM-1] Entry\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-2] Check params\n");
    __asm__ volatile ("" ::: "memory");
    if (!bitmap_virt || memory_end <= memory_start) {
        return -1;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-3] Align boundaries\n");
    __asm__ volatile ("" ::: "memory");
    /* Align to page boundaries */
    memory_start = (memory_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    memory_end = memory_end & ~(PAGE_SIZE - 1);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-4] Calculate pages\n");
    __asm__ volatile ("" ::: "memory");
    /* Calculate number of pages */
    uint64_t total_bytes = memory_end - memory_start;
    uint64_t total_pages = total_bytes / PAGE_SIZE;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-5] Calculate bitmap size\n");
    __asm__ volatile ("" ::: "memory");
    /* Calculate bitmap size (1 bit per page) */
    uint64_t bitmap_size = (total_pages + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
    bitmap_size = (bitmap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); /* Align to page */
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-6] Init state\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize state */
    pmm_state.total_pages = total_pages;
    pmm_state.free_pages = total_pages;
    pmm_state.used_pages = 0;
    pmm_state.bitmap = (uint32_t*)bitmap_virt;
    pmm_state.bitmap_size = bitmap_size;
    pmm_state.memory_start = memory_start;
    pmm_state.memory_end = memory_end;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-7] Clear bitmap\n");
    __asm__ volatile ("" ::: "memory");
    /* Clear bitmap (mark all pages as free) - XNU-style: efficient clearing */
    /* Use volatile pointer to prevent optimization issues */
    volatile uint32_t* bitmap_ptr = (volatile uint32_t*)pmm_state.bitmap;
    uint64_t bitmap_words = bitmap_size / sizeof(uint32_t);
    
    /* Clear in chunks to avoid long loops (XNU-style: batched operations) */
    const uint64_t chunk_size = 1024; /* Clear 1024 words at a time */
    uint64_t chunks = bitmap_words / chunk_size;
    uint64_t remainder = bitmap_words % chunk_size;
    
    kputs("[PMM-7.1] Clear chunks\n");
    __asm__ volatile ("" ::: "memory");
    for (uint64_t chunk = 0; chunk < chunks; chunk++) {
        uint64_t start = chunk * chunk_size;
        for (uint64_t i = 0; i < chunk_size; i++) {
            bitmap_ptr[start + i] = 0;
        }
        __asm__ volatile ("" ::: "memory");
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-7.2] Clear remainder\n");
    __asm__ volatile ("" ::: "memory");
    /* Clear remainder */
    uint64_t start = chunks * chunk_size;
    for (uint64_t i = 0; i < remainder; i++) {
        bitmap_ptr[start + i] = 0;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-OK] Done\n");
    return 0;
}

/**
 * @function pmm_alloc_page
 * @brief Allocate a single physical page
 * 
 * This function implements a first-fit allocation strategy, similar to XNU's
 * approach. It finds the first free page and marks it as used.
 * 
 * @return Physical address of allocated page, or 0 on failure
 * 
 * @note The returned address is page-aligned.
 * @note In XNU-style, this would be part of a zone-based allocator.
 *       For now, we use a simple bitmap-based approach.
 */
uint64_t pmm_alloc_page(void)
{
    if (pmm_state.free_pages == 0) {
        return 0; /* No free pages */
    }
    
    /* Find first free page (first-fit algorithm) */
    for (uint64_t i = 0; i < pmm_state.total_pages; i++) {
        if (!pmm_bitmap_test(i)) {
            /* Mark as used */
            pmm_bitmap_set(i);
            pmm_state.free_pages--;
            pmm_state.used_pages++;
            
            uint64_t phys = pmm_index_to_page(i);
            
            /* Zero the page (XNU-style: ensure clean pages) */
            void* virt = (void*)(phys + X86_64_KERNEL_VIRT_BASE);
            for (uint64_t j = 0; j < PAGE_SIZE / sizeof(uint64_t); j++) {
                ((uint64_t*)virt)[j] = 0;
            }
            
            return phys;
        }
    }
    
    return 0; /* No free page found */
}

/**
 * @function pmm_free_page
 * @brief Free a single physical page
 * 
 * @param phys Physical address of page to free
 * 
 * @note The address must be page-aligned and within managed range.
 */
void pmm_free_page(uint64_t phys)
{
    if (phys < pmm_state.memory_start || phys >= pmm_state.memory_end) {
        return; /* Invalid address */
    }
    
    if ((phys & (PAGE_SIZE - 1)) != 0) {
        return; /* Not page-aligned */
    }
    
    uint64_t index = pmm_page_to_index(phys);
    
    if (pmm_bitmap_test(index)) {
        /* Mark as free */
        pmm_bitmap_clear(index);
        pmm_state.free_pages++;
        pmm_state.used_pages--;
    }
}

/**
 * @function pmm_alloc_pages
 * @brief Allocate multiple contiguous physical pages
 * 
 * This function attempts to allocate contiguous physical pages, which is
 * important for large page mappings (2MB, 1GB) and DMA buffers.
 * 
 * @param count Number of pages to allocate
 * @return Physical address of first page, or 0 on failure
 * 
 * @note This implements a best-fit contiguous allocation algorithm.
 *       XNU uses zone-based allocation for better performance, but this
 *       simple approach works for initial implementation.
 */
uint64_t pmm_alloc_pages(uint32_t count)
{
    if (count == 0 || pmm_state.free_pages < count) {
        return 0;
    }
    
    /* Find contiguous free pages */
    for (uint64_t start = 0; start <= pmm_state.total_pages - count; start++) {
        bool found = true;
        
        /* Check if we have 'count' contiguous free pages starting at 'start' */
        for (uint32_t i = 0; i < count; i++) {
            if (pmm_bitmap_test(start + i)) {
                found = false;
                start += i; /* Skip past used page */
                break;
            }
        }
        
        if (found) {
            /* Mark all pages as used */
            for (uint32_t i = 0; i < count; i++) {
                pmm_bitmap_set(start + i);
            }
            
            pmm_state.free_pages -= count;
            pmm_state.used_pages += count;
            
            uint64_t first_page = pmm_index_to_page(start);
            
            /* Zero all pages (XNU-style: ensure clean pages) */
            for (uint32_t i = 0; i < count; i++) {
                void* virt = (void*)(first_page + i * PAGE_SIZE + X86_64_KERNEL_VIRT_BASE);
                for (uint64_t j = 0; j < PAGE_SIZE / sizeof(uint64_t); j++) {
                    ((uint64_t*)virt)[j] = 0;
                }
            }
            
            return first_page;
        }
    }
    
    return 0; /* No contiguous block found */
}

/**
 * @function pmm_free_pages
 * @brief Free multiple physical pages
 * 
 * @param phys Physical address of first page
 * @param count Number of pages to free
 */
void pmm_free_pages(uint64_t phys, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        pmm_free_page(phys + i * PAGE_SIZE);
    }
}

/**
 * @function pmm_get_total_pages
 * @brief Get total number of physical pages
 * 
 * @return Total number of pages
 */
uint64_t pmm_get_total_pages(void)
{
    return pmm_state.total_pages;
}

/**
 * @function pmm_get_free_pages
 * @brief Get number of free pages
 * 
 * @return Number of free pages
 */
uint64_t pmm_get_free_pages(void)
{
    return pmm_state.free_pages;
}

/**
 * @function pmm_get_used_pages
 * @brief Get number of used pages
 * 
 * @return Number of used pages
 */
uint64_t pmm_get_used_pages(void)
{
    return pmm_state.used_pages;
}

