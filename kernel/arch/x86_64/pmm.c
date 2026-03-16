/**
 * @file pmm.c
 * @brief Physical Memory Manager (PMM) implementation for x86_64
 * 
 * This module implements physical memory management using a bitmap-based
 * allocator. It tracks free and used physical pages and provides allocation
 * and deallocation functions.
 * 
 * @note This implementation is adapted for RodNIX.
 */

#include "types.h"
#include "pmm.h"
#include "config.h"
#include "../../../include/debug.h"
#include "../../../include/error.h"
#include "../../core/memory.h"
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

/* Multiboot2 mmap types */
#define MB2_MMAP_AVAILABLE 1
#define MB2_MMAP_RESERVED  2
#define MB2_MMAP_ACPI      3
#define MB2_MMAP_NVS       4
#define MB2_MMAP_BADRAM    5

/* Fixed bitmap storage cap (matches low-memory placement) */
#define PMM_BITMAP_MAX_SIZE 0x100000ULL
#define PMM_MAX_FREE_RANGES 128
#define PMM_MAX_REGIONS     128

/* Page descriptor states */
typedef enum {
    PMM_PAGE_FREE = 0,
    PMM_PAGE_USED = 1
} pmm_page_state_t;

typedef struct {
    uint64_t start;
    uint64_t count;
} pmm_free_range_t;

typedef struct {
    uint64_t phys;
    uint8_t zone;
    uint8_t state;
    uint16_t reserved;
} pmm_page_desc_t;

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
    pmm_page_desc_t* pages;      /* Page descriptors */
    uint64_t pages_count;        /* Number of page descriptors */
    struct {
        uint64_t total_pages;
        uint64_t free_pages;
        uint64_t used_pages;
        uint32_t free_range_count;
        pmm_free_range_t free_ranges[PMM_MAX_FREE_RANGES];
    } zones[PMM_ZONE_COUNT];

    pmm_region_t usable_regions[PMM_MAX_REGIONS];
    pmm_region_t reserved_regions[PMM_MAX_REGIONS];
    uint32_t usable_count;
    uint32_t reserved_count;
};

/* Global PMM state */
static struct pmm_state pmm_state = {0};

/* Multiboot2 memory map entry (minimal) */
struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

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

static void pmm_bitmap_set_all(void)
{
    uint64_t bitmap_words = pmm_state.bitmap_size / sizeof(uint32_t);
    for (uint64_t i = 0; i < bitmap_words; i++) {
        pmm_state.bitmap[i] = 0xFFFFFFFFU;
    }
}

static void pmm_zero_page(uint64_t phys)
{
    volatile uint64_t* ptr = (volatile uint64_t*)X86_64_PHYS_TO_VIRT(phys);
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        ptr[i] = 0;
    }
}

static inline pmm_zone_t pmm_zone_for_addr(uint64_t addr)
{
    /* Simple split: low memory below 16MB, everything else is normal. */
    if (addr < 0x1000000ULL) {
        return PMM_ZONE_LOW;
    }
    return PMM_ZONE_NORMAL;
}

static void pmm_regions_clear(void)
{
    pmm_state.usable_count = 0;
    pmm_state.reserved_count = 0;
}

static void pmm_add_region(pmm_region_t* regions, uint32_t* count,
                           uint64_t start, uint64_t length)
{
    if (!regions || !count || length == 0) {
        return;
    }
    if (*count >= PMM_MAX_REGIONS) {
        return;
    }
    regions[*count].base = start;
    regions[*count].length = length;
    (*count)++;
}

static void pmm_mark_range_mmio(uint64_t start, uint64_t end)
{
    if (pmm_state.pages_count == 0) {
        return;
    }
    if (end <= start) {
        return;
    }
    if (end <= pmm_state.memory_start || start >= pmm_state.memory_end) {
        return;
    }
    if (start < pmm_state.memory_start) {
        start = pmm_state.memory_start;
    }
    if (end > pmm_state.memory_end) {
        end = pmm_state.memory_end;
    }
    start = start & ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t index = pmm_page_to_index(addr);
        if (index < pmm_state.pages_count) {
            pmm_state.pages[index].zone = (uint8_t)PMM_ZONE_MMIO;
        }
    }
}

static void pmm_freelist_clear(void)
{
    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        pmm_state.zones[z].free_range_count = 0;
    }
}

static void pmm_freelist_append(pmm_zone_t zone, uint64_t start, uint64_t count)
{
    if (count == 0) {
        return;
    }
    if (pmm_state.zones[zone].free_range_count >= PMM_MAX_FREE_RANGES) {
        return;
    }
    pmm_free_range_t* r = &pmm_state.zones[zone].free_ranges[pmm_state.zones[zone].free_range_count++];
    r->start = start;
    r->count = count;
}

static void pmm_freelist_insert(pmm_zone_t zone, uint64_t start, uint64_t count)
{
    if (count == 0) {
        return;
    }

    uint32_t n = pmm_state.zones[zone].free_range_count;
    if (n == 0) {
        pmm_freelist_append(zone, start, count);
        return;
    }

    uint32_t pos = 0;
    while (pos < n && pmm_state.zones[zone].free_ranges[pos].start < start) {
        pos++;
    }

    if (n >= PMM_MAX_FREE_RANGES) {
        return;
    }

    for (uint32_t i = n; i > pos; i--) {
        pmm_state.zones[zone].free_ranges[i] = pmm_state.zones[zone].free_ranges[i - 1];
    }
    pmm_state.zones[zone].free_ranges[pos].start = start;
    pmm_state.zones[zone].free_ranges[pos].count = count;
    pmm_state.zones[zone].free_range_count++;

    /* Merge with previous/next if adjacent */
    if (pos > 0) {
        pmm_free_range_t* prev = &pmm_state.zones[zone].free_ranges[pos - 1];
        pmm_free_range_t* cur = &pmm_state.zones[zone].free_ranges[pos];
        if (prev->start + prev->count == cur->start) {
            prev->count += cur->count;
            for (uint32_t i = pos; i + 1 < pmm_state.zones[zone].free_range_count; i++) {
                pmm_state.zones[zone].free_ranges[i] = pmm_state.zones[zone].free_ranges[i + 1];
            }
            pmm_state.zones[zone].free_range_count--;
            pos--;
        }
    }
    if (pos + 1 < pmm_state.zones[zone].free_range_count) {
        pmm_free_range_t* cur = &pmm_state.zones[zone].free_ranges[pos];
        pmm_free_range_t* next = &pmm_state.zones[zone].free_ranges[pos + 1];
        if (cur->start + cur->count == next->start) {
            cur->count += next->count;
            for (uint32_t i = pos + 1; i + 1 < pmm_state.zones[zone].free_range_count; i++) {
                pmm_state.zones[zone].free_ranges[i] = pmm_state.zones[zone].free_ranges[i + 1];
            }
            pmm_state.zones[zone].free_range_count--;
        }
    }
}

static bool pmm_freelist_remove(pmm_zone_t zone, uint64_t start, uint64_t count)
{
    if (count == 0) {
        return false;
    }
    uint32_t n = pmm_state.zones[zone].free_range_count;
    for (uint32_t i = 0; i < n; i++) {
        pmm_free_range_t* r = &pmm_state.zones[zone].free_ranges[i];
        if (start >= r->start && start + count <= r->start + r->count) {
            uint64_t tail_start = start + count;
            uint64_t tail_count = (r->start + r->count) - tail_start;
            uint64_t head_count = start - r->start;

            if (head_count > 0 && tail_count > 0) {
                r->count = head_count;
                if (pmm_state.zones[zone].free_range_count < PMM_MAX_FREE_RANGES) {
                    for (uint32_t j = n; j > i + 1; j--) {
                        pmm_state.zones[zone].free_ranges[j] = pmm_state.zones[zone].free_ranges[j - 1];
                    }
                    pmm_state.zones[zone].free_ranges[i + 1].start = tail_start;
                    pmm_state.zones[zone].free_ranges[i + 1].count = tail_count;
                    pmm_state.zones[zone].free_range_count++;
                }
            } else if (head_count > 0) {
                r->count = head_count;
            } else if (tail_count > 0) {
                r->start = tail_start;
                r->count = tail_count;
            } else {
                for (uint32_t j = i; j + 1 < n; j++) {
                    pmm_state.zones[zone].free_ranges[j] = pmm_state.zones[zone].free_ranges[j + 1];
                }
                pmm_state.zones[zone].free_range_count--;
            }
            return true;
        }
    }
    return false;
}

static void pmm_rebuild_free_lists(void)
{
    pmm_freelist_clear();

    uint64_t run_start = 0;
    uint64_t run_count = 0;
    pmm_zone_t run_zone = PMM_ZONE_NORMAL;

    for (uint64_t i = 0; i < pmm_state.total_pages; i++) {
        if (!pmm_bitmap_test(i)) {
            uint64_t phys = pmm_index_to_page(i);
            pmm_zone_t zone = pmm_zone_for_addr(phys);
            if (run_count == 0) {
                run_start = i;
                run_count = 1;
                run_zone = zone;
            } else if (zone == run_zone && run_start + run_count == i) {
                run_count++;
            } else {
                pmm_freelist_append(run_zone, run_start, run_count);
                run_start = i;
                run_count = 1;
                run_zone = zone;
            }
        } else if (run_count > 0) {
            pmm_freelist_append(run_zone, run_start, run_count);
            run_count = 0;
        }
    }

    if (run_count > 0) {
        pmm_freelist_append(run_zone, run_start, run_count);
    }
}

static void pmm_setup_page_descs(uint64_t memory_start, uint64_t memory_end,
                                 uint64_t bitmap_phys,
                                 uint64_t bitmap_size)
{
    uint64_t total_bytes = memory_end - memory_start;
    uint64_t total_pages = total_bytes / PAGE_SIZE;
    uint64_t desc_size = total_pages * sizeof(pmm_page_desc_t);
    uint64_t desc_phys = (bitmap_phys + bitmap_size + 7) & ~7ULL;

    /* If descriptors do not fit safely in low memory, skip for now. */
    if (desc_phys + desc_size > 0x1000000ULL) {
        pmm_state.pages = NULL;
        pmm_state.pages_count = 0;
        return;
    }

    /* Descriptors are placed in low memory; access via higher-half direct map. */
    pmm_state.pages = (pmm_page_desc_t*)X86_64_PHYS_TO_VIRT(desc_phys);
    pmm_state.pages_count = total_pages;

    for (uint64_t i = 0; i < total_pages; i++) {
        uint64_t phys = memory_start + (i * PAGE_SIZE);
        pmm_state.pages[i].phys = phys;
        pmm_state.pages[i].zone = (uint8_t)pmm_zone_for_addr(phys);
        pmm_state.pages[i].state = PMM_PAGE_USED;
        pmm_state.pages[i].reserved = 0;
    }
}

static void pmm_mark_range_free(uint64_t start, uint64_t end)
{
    if (end <= start) {
        return;
    }
    if (end <= pmm_state.memory_start || start >= pmm_state.memory_end) {
        return;
    }
    if (start < pmm_state.memory_start) {
        start = pmm_state.memory_start;
    }
    if (end > pmm_state.memory_end) {
        end = pmm_state.memory_end;
    }
    start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    end = end & ~(PAGE_SIZE - 1);
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t index = pmm_page_to_index(addr);
        if (pmm_bitmap_test(index)) {
            pmm_bitmap_clear(index);
            pmm_state.free_pages++;
            pmm_state.used_pages--;
            if (index < pmm_state.pages_count) {
                pmm_state.pages[index].state = PMM_PAGE_FREE;
                pmm_zone_t zone = (pmm_zone_t)pmm_state.pages[index].zone;
                pmm_state.zones[zone].free_pages++;
                pmm_state.zones[zone].used_pages--;
            }
        }
    }
}

static void pmm_mark_range_used(uint64_t start, uint64_t end)
{
    if (end <= start) {
        return;
    }
    if (end <= pmm_state.memory_start || start >= pmm_state.memory_end) {
        return;
    }
    if (start < pmm_state.memory_start) {
        start = pmm_state.memory_start;
    }
    if (end > pmm_state.memory_end) {
        end = pmm_state.memory_end;
    }
    start = start & ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t index = pmm_page_to_index(addr);
        if (!pmm_bitmap_test(index)) {
            pmm_bitmap_set(index);
            pmm_state.free_pages--;
            pmm_state.used_pages++;
            if (index < pmm_state.pages_count) {
                pmm_state.pages[index].state = PMM_PAGE_USED;
                pmm_zone_t zone = (pmm_zone_t)pmm_state.pages[index].zone;
                pmm_state.zones[zone].free_pages--;
                pmm_state.zones[zone].used_pages++;
            }
        }
    }
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
        return RDNX_E_INVALID;
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
    kputs("[PMM-4.1] Calc total_bytes\n");
    __asm__ volatile ("" ::: "memory");
    uint64_t total_bytes = memory_end - memory_start;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-4.2] Calc total_pages\n");
    __asm__ volatile ("" ::: "memory");
    uint64_t total_pages = total_bytes / PAGE_SIZE;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-4.3] Pages calculated\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-5] Calculate bitmap size\n");
    __asm__ volatile ("" ::: "memory");
    /* Calculate bitmap size (1 bit per page) */
    kputs("[PMM-5.1] Calc bitmap_size\n");
    __asm__ volatile ("" ::: "memory");
    uint64_t bitmap_size = (total_pages + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-5.2] Align bitmap_size\n");
    __asm__ volatile ("" ::: "memory");
    bitmap_size = (bitmap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); /* Align to page */
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-5.3] Bitmap size calculated\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-6] Init state\n");
    __asm__ volatile ("" ::: "memory");
    /* Initialize state */
    kputs("[PMM-6.1] Set total_pages\n");
    __asm__ volatile ("" ::: "memory");
    pmm_state.total_pages = total_pages;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-6.2] Set free_pages\n");
    __asm__ volatile ("" ::: "memory");
    pmm_state.free_pages = total_pages;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-6.3] Set used_pages\n");
    __asm__ volatile ("" ::: "memory");
    pmm_state.used_pages = 0;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-6.4] Set bitmap\n");
    __asm__ volatile ("" ::: "memory");
    pmm_state.bitmap = (uint32_t*)bitmap_virt;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-6.5] Set bitmap_size\n");
    __asm__ volatile ("" ::: "memory");
    pmm_state.bitmap_size = bitmap_size;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-6.6] Set memory_start\n");
    __asm__ volatile ("" ::: "memory");
    pmm_state.memory_start = memory_start;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-6.7] Set memory_end\n");
    __asm__ volatile ("" ::: "memory");
    pmm_state.memory_end = memory_end;
    __asm__ volatile ("" ::: "memory");

    pmm_regions_clear();

    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        pmm_state.zones[z].total_pages = 0;
        pmm_state.zones[z].free_pages = 0;
        pmm_state.zones[z].used_pages = 0;
    }
    
    kputs("[PMM-6.8] State initialized\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[PMM-7] Clear bitmap\n");
    __asm__ volatile ("" ::: "memory");
    /* Clear bitmap (mark all pages as free) - efficient clearing */
    /* Use volatile pointer to prevent optimization issues */
    volatile uint32_t* bitmap_ptr = (volatile uint32_t*)pmm_state.bitmap;
    uint64_t bitmap_words = bitmap_size / sizeof(uint32_t);
    
    /* Clear in chunks to avoid long loops (batched operations) */
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

    pmm_setup_page_descs(memory_start, memory_end,
                         (uint64_t)((uintptr_t)bitmap_virt),
                         bitmap_size);
    if (pmm_state.pages_count > 0) {
        for (uint64_t i = 0; i < pmm_state.pages_count; i++) {
            pmm_state.pages[i].state = PMM_PAGE_FREE;
            pmm_zone_t zone = (pmm_zone_t)pmm_state.pages[i].zone;
            pmm_state.zones[zone].total_pages++;
            pmm_state.zones[zone].free_pages++;
        }
    }

    pmm_add_region(pmm_state.usable_regions, &pmm_state.usable_count,
                   memory_start, memory_end - memory_start);

    pmm_rebuild_free_lists();
    return 0;
}

/**
 * @function pmm_init_from_mmap
 * @brief Initialize PMM using Multiboot2 memory map
 */
int pmm_init_from_mmap(uint64_t memory_start, uint64_t memory_end,
                       void* bitmap_virt, uint64_t bitmap_phys,
                       const void* mmap_tag, uint32_t mmap_size, uint32_t entry_size)
{
    if (!bitmap_virt || !mmap_tag || memory_end <= memory_start || entry_size == 0) {
        return RDNX_E_INVALID;
    }

    /* Align to page boundaries */
    memory_start = (memory_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    memory_end = memory_end & ~(PAGE_SIZE - 1);
    uint64_t total_bytes = memory_end - memory_start;
    uint64_t total_pages = total_bytes / PAGE_SIZE;

    uint64_t bitmap_size = (total_pages + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
    bitmap_size = (bitmap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (bitmap_size > PMM_BITMAP_MAX_SIZE) {
        uint64_t max_pages = PMM_BITMAP_MAX_SIZE * 8ULL;
        memory_end = memory_start + (max_pages * PAGE_SIZE);
        total_bytes = memory_end - memory_start;
        total_pages = total_bytes / PAGE_SIZE;
        bitmap_size = (total_pages + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
        bitmap_size = (bitmap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }
    pmm_state.total_pages = total_pages;
    pmm_state.free_pages = 0;
    pmm_state.used_pages = total_pages;
    pmm_state.bitmap = (uint32_t*)bitmap_virt;
    pmm_state.bitmap_size = bitmap_size;
    pmm_state.memory_start = memory_start;
    pmm_state.memory_end = memory_end;
    /* Mark all pages as used, then free only available ranges */
    pmm_bitmap_set_all();

    pmm_regions_clear();

    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        pmm_state.zones[z].total_pages = 0;
        pmm_state.zones[z].free_pages = 0;
        pmm_state.zones[z].used_pages = 0;
    }
    pmm_setup_page_descs(memory_start, memory_end, bitmap_phys, bitmap_size);

    const uint8_t* base = (const uint8_t*)mmap_tag;
    uint32_t offset = sizeof(uint32_t) * 4; /* type, size, entry_size, entry_version */
    if (mmap_size <= offset) {
        return RDNX_E_INVALID;
    }

    uint32_t max_entries = (mmap_size - offset) / entry_size;
    if (max_entries > 4096) {
        max_entries = 4096;
    }

    if (pmm_state.pages_count > 0) {
        uint32_t mmio_off = offset;
        for (uint32_t i = 0; i < max_entries; i++) {
            const struct mb2_mmap_entry* e = (const struct mb2_mmap_entry*)(base + mmio_off);
            if (e->len != 0 && e->addr + e->len >= e->addr) {
                if (e->type == MB2_MMAP_AVAILABLE) {
                    pmm_add_region(pmm_state.usable_regions, &pmm_state.usable_count,
                                   e->addr, e->len);
                } else {
                    pmm_add_region(pmm_state.reserved_regions, &pmm_state.reserved_count,
                                   e->addr, e->len);
                    pmm_mark_range_mmio(e->addr, e->addr + e->len);
                }
            }
            mmio_off += entry_size;
        }

        for (uint64_t i = 0; i < pmm_state.pages_count; i++) {
            pmm_state.pages[i].state = PMM_PAGE_USED;
            pmm_zone_t zone = (pmm_zone_t)pmm_state.pages[i].zone;
            pmm_state.zones[zone].total_pages++;
            pmm_state.zones[zone].used_pages++;
        }
    }

    uint32_t free_off = offset;
    for (uint32_t i = 0; i < max_entries; i++) {
        const struct mb2_mmap_entry* e = (const struct mb2_mmap_entry*)(base + free_off);
        if (e->type == MB2_MMAP_AVAILABLE) {
            uint64_t addr = e->addr;
            uint64_t len = e->len;
            if (len != 0 && addr + len >= addr) {
                pmm_mark_range_free(addr, addr + len);
            }
        }
        free_off += entry_size;
    }

    /* Keep bitmap and descriptors reserved */
    pmm_mark_range_used(bitmap_phys, bitmap_phys + bitmap_size);
    pmm_add_region(pmm_state.reserved_regions, &pmm_state.reserved_count,
                   bitmap_phys, bitmap_size);
    if (pmm_state.pages_count > 0) {
        uint64_t desc_size = pmm_state.pages_count * sizeof(pmm_page_desc_t);
        uint64_t desc_phys = (bitmap_phys + bitmap_size + 7) & ~7ULL;
        pmm_mark_range_used(desc_phys, desc_phys + desc_size);
        pmm_add_region(pmm_state.reserved_regions, &pmm_state.reserved_count,
                       desc_phys, desc_size);
    }

    pmm_rebuild_free_lists();
    return RDNX_OK;
}

/**
 * @function pmm_alloc_page
 * @brief Allocate a single physical page
 * 
 * This function implements a first-fit allocation strategy.
 * It finds the first free page and marks it as used.
 * 
 * @return Physical address of allocated page, or 0 on failure
 * 
 * @note The returned address is page-aligned.
 * @note This would be part of a zone-based allocator in a more advanced implementation.
 *       For now, we use a simple bitmap-based approach.
 */
uint64_t pmm_alloc_page(void)
{
    uint64_t phys = pmm_alloc_page_in_zone(PMM_ZONE_NORMAL);
    if (phys) {
        return phys;
    }
    return pmm_alloc_page_in_zone(PMM_ZONE_LOW);
}

uint64_t pmm_alloc_page_in_zone(pmm_zone_t zone)
{
    if (pmm_state.free_pages == 0 || zone >= PMM_ZONE_COUNT) {
        TRACE_EVENT("oom: pmm_alloc_page");
        memory_oom_inc_pmm();
        return 0;
    }

    uint32_t n = pmm_state.zones[zone].free_range_count;
    uint64_t index = 0;
    bool found = false;
    if (n > 0) {
        pmm_free_range_t* r = &pmm_state.zones[zone].free_ranges[0];
        index = r->start;
        found = pmm_freelist_remove(zone, index, 1);
    }
    if (!found) {
        for (uint64_t i = 0; i < pmm_state.total_pages; i++) {
            if (!pmm_bitmap_test(i)) {
                uint64_t phys = pmm_index_to_page(i);
                if (pmm_zone_for_addr(phys) == zone) {
                    index = i;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        TRACE_EVENT("oom: pmm_alloc_page");
        memory_oom_inc_pmm();
        return 0;
    }

    pmm_bitmap_set(index);
    pmm_state.free_pages--;
    pmm_state.used_pages++;

    uint64_t phys = pmm_index_to_page(index);
    pmm_zero_page(phys);

    if (index < pmm_state.pages_count) {
        pmm_state.pages[index].state = PMM_PAGE_USED;
        if (pmm_state.zones[zone].free_pages > 0) {
            pmm_state.zones[zone].free_pages--;
            pmm_state.zones[zone].used_pages++;
        }
    }

    return phys;
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
        pmm_zone_t zone = pmm_zone_for_addr(phys);
        if (index < pmm_state.pages_count) {
            pmm_state.pages[index].state = PMM_PAGE_FREE;
            pmm_state.zones[zone].free_pages++;
            if (pmm_state.zones[zone].used_pages > 0) {
                pmm_state.zones[zone].used_pages--;
            }
        }
        pmm_freelist_insert(zone, index, 1);
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
 *       Zone-based allocation would provide better performance, but this
 *       simple approach works for initial implementation.
 */
uint64_t pmm_alloc_pages(uint32_t count)
{
    uint64_t phys = pmm_alloc_pages_in_zone(PMM_ZONE_NORMAL, count);
    if (phys) {
        return phys;
    }
    return pmm_alloc_pages_in_zone(PMM_ZONE_LOW, count);
}

uint64_t pmm_alloc_pages_in_zone(pmm_zone_t zone, uint32_t count)
{
    if (count == 0 || zone >= PMM_ZONE_COUNT) {
        return 0;
    }
    if (pmm_state.free_pages < count) {
        return 0;
    }

    uint32_t n = pmm_state.zones[zone].free_range_count;
    for (uint32_t i = 0; i < n; i++) {
        pmm_free_range_t* r = &pmm_state.zones[zone].free_ranges[i];
        if (r->count >= count) {
            uint64_t start = r->start;
            if (!pmm_freelist_remove(zone, start, count)) {
                return 0;
            }
            for (uint32_t p = 0; p < count; p++) {
                uint64_t idx = start + p;
                pmm_bitmap_set(idx);
                if (idx < pmm_state.pages_count) {
                    pmm_state.pages[idx].state = PMM_PAGE_USED;
                }
            }

            pmm_state.free_pages -= count;
            pmm_state.used_pages += count;
            if (pmm_state.zones[zone].free_pages >= count) {
                pmm_state.zones[zone].free_pages -= count;
                pmm_state.zones[zone].used_pages += count;
            }

            uint64_t phys = pmm_index_to_page(start);
            for (uint32_t p = 0; p < count; p++) {
                pmm_zero_page(phys + (uint64_t)p * PAGE_SIZE);
            }

            return pmm_index_to_page(start);
        }
    }

    /*
     * Fallback path: if free-list metadata is stale/trimmed, scan bitmap
     * directly for a contiguous run in the requested zone.
     */
    uint64_t run_start = 0;
    uint32_t run_count = 0;
    bool found = false;
    for (uint64_t i = 0; i < pmm_state.total_pages; i++) {
        if (pmm_bitmap_test(i)) {
            run_count = 0;
            continue;
        }
        uint64_t phys = pmm_index_to_page(i);
        if (pmm_zone_for_addr(phys) != zone) {
            run_count = 0;
            continue;
        }
        if (run_count == 0) {
            run_start = i;
        }
        run_count++;
        if (run_count >= count) {
            found = true;
            break;
        }
    }

    if (!found) {
        return 0;
    }

    for (uint32_t p = 0; p < count; p++) {
        uint64_t idx = run_start + p;
        pmm_bitmap_set(idx);
        if (idx < pmm_state.pages_count) {
            pmm_state.pages[idx].state = PMM_PAGE_USED;
        }
    }

    pmm_state.free_pages -= count;
    pmm_state.used_pages += count;
    if (pmm_state.zones[zone].free_pages >= count) {
        pmm_state.zones[zone].free_pages -= count;
        pmm_state.zones[zone].used_pages += count;
    }

    uint64_t phys = pmm_index_to_page(run_start);
    for (uint32_t p = 0; p < count; p++) {
        pmm_zero_page(phys + (uint64_t)p * PAGE_SIZE);
    }

    /* Re-sync free-list metadata after bitmap fallback allocation. */
    pmm_rebuild_free_lists();
    return phys;
}

void pmm_reserve_range(uint64_t start, uint64_t end)
{
    if (end <= start) {
        return;
    }

    pmm_mark_range_used(start, end);
    pmm_add_region(pmm_state.reserved_regions, &pmm_state.reserved_count,
                   start, end - start);
    pmm_rebuild_free_lists();
}

void pmm_release_range(uint64_t start, uint64_t end)
{
    if (end <= start) {
        return;
    }
    pmm_mark_range_free(start, end);
    pmm_rebuild_free_lists();
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

int pmm_get_zone_stats(pmm_zone_t zone, pmm_zone_stats_t* out)
{
    if (!out || zone < 0 || zone >= PMM_ZONE_COUNT) {
        return RDNX_E_INVALID;
    }
    out->total_pages = pmm_state.zones[zone].total_pages;
    out->free_pages = pmm_state.zones[zone].free_pages;
    out->used_pages = pmm_state.zones[zone].used_pages;
    return RDNX_OK;
}

int pmm_get_free_regions(pmm_zone_t zone, pmm_region_t* out, uint32_t max,
                         uint32_t* out_count)
{
    if (!out_count || zone >= PMM_ZONE_COUNT) {
        return RDNX_E_INVALID;
    }
    uint32_t n = pmm_state.zones[zone].free_range_count;
    if (!out || max == 0) {
        *out_count = n;
        return RDNX_OK;
    }
    uint32_t count = (n < max) ? n : max;
    for (uint32_t i = 0; i < count; i++) {
        const pmm_free_range_t* r = &pmm_state.zones[zone].free_ranges[i];
        out[i].base = pmm_index_to_page(r->start);
        out[i].length = r->count * PAGE_SIZE;
    }
    *out_count = count;
    return RDNX_OK;
}

int pmm_get_usable_regions(pmm_region_t* out, uint32_t max, uint32_t* out_count)
{
    if (!out_count) {
        return RDNX_E_INVALID;
    }
    uint32_t n = pmm_state.usable_count;
    if (!out || max == 0) {
        *out_count = n;
        return RDNX_OK;
    }
    uint32_t count = (n < max) ? n : max;
    for (uint32_t i = 0; i < count; i++) {
        out[i] = pmm_state.usable_regions[i];
    }
    *out_count = count;
    return RDNX_OK;
}

int pmm_get_reserved_regions(pmm_region_t* out, uint32_t max, uint32_t* out_count)
{
    if (!out_count) {
        return RDNX_E_INVALID;
    }
    uint32_t n = pmm_state.reserved_count;
    if (!out || max == 0) {
        *out_count = n;
        return RDNX_OK;
    }
    uint32_t count = (n < max) ? n : max;
    for (uint32_t i = 0; i < count; i++) {
        out[i] = pmm_state.reserved_regions[i];
    }
    *out_count = count;
    return RDNX_OK;
}
