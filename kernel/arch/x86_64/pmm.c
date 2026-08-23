/**
 * @file pmm.c
 * @brief Physical Memory Manager (PMM) implementation for x86_64
 * 
 * This module implements physical memory management using buddy free lists
 * allocator. It tracks free and used physical pages and provides allocation
 * and deallocation functions.
 * 
 * @note This implementation is adapted for RodNIX.
 */

#include "types.h"
#include "../../fabric/spin.h"
#include "pmm.h"
#include "config.h"
#include "../../../include/debug.h"
#include "../../../include/error.h"
#include "../../core/memory.h"
#include "../../../mm/vm_page.h"
#include "../../../mm/vm_phys.h"
#include "../../../trace/bootlog.h"
#include "../../../include/console.h"
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

/* Retained only to size the bootstrap allocation memory.c makes for us. */
#define PMM_BITMAP_MAX_SIZE 0x100000ULL
#define PMM_MAX_REGIONS     128

/**
 * @struct pmm_state
 * @brief PMM internal state
 */
struct pmm_state {
    uint64_t total_pages;        /* Total number of physical pages */
    uint64_t free_pages;         /* Number of free pages */
    uint64_t used_pages;         /* Number of used pages */
    uint64_t memory_start;       /* Start of managed memory */
    uint64_t memory_end;         /* End of managed memory */
    /* One entry per physical page, owned by the machine-independent layer
     * (mm/vm_page.h). This layer allocates the storage -- it is the one that
     * knows where physical memory is and how to address it -- and then only
     * writes the two fields that describe residency. */
    vm_page_t* pages;
    uint64_t pages_count;
    struct {
        uint64_t total_pages;
        uint64_t free_pages;
        uint64_t used_pages;
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
 * @function pmm_page_to_index
 * @brief Convert physical address to page index
 * 
 * @param phys Physical address
 * @return Page index
 */
/*
 * Is this page in the allocator's hands?
 *
 * This replaces the page bitmap, which was a second record of the same fact.
 * The buddy already knows -- a page is free exactly when some free block
 * covers it -- and asking costs a walk of at most VM_NFREEORDER aligned bases
 * rather than a bit test. Bounded, and one source of truth instead of two that
 * can disagree.
 */
static inline bool pmm_page_is_free(uint64_t phys)
{
    return vm_phys_is_free(phys);
}

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


static void pmm_zero_page(uint64_t phys)
{
    volatile uint64_t* ptr = (volatile uint64_t*)X86_64_PHYS_TO_VIRT(phys);
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        ptr[i] = 0;
    }
}

/* Where the low zone ends. A buddy block is naturally aligned and this is a
 * power of two, so no block ever straddles it. */
#define PMM_LOW_ZONE_LIMIT 0x1000000ULL

static inline pmm_zone_t pmm_zone_for_addr(uint64_t addr)
{
    /* Simple split: low memory below 16MB, everything else is normal. */
    if (addr < PMM_LOW_ZONE_LIMIT) {
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





/*
 * Take a run of pages out of the free lists, however many listed ranges it
 * happens to straddle.
 *
 * pmm_freelist_remove() only handles a run that lies inside one range and
 * reports failure otherwise. Used on its own that would be a correctness bug
 * rather than a missed optimisation: a run spanning two ranges would be
 * removed from neither, and the allocator would go on handing out pages that
 * are now reserved.
 *
 * A page that no range covers is skipped. That is not an error -- the lists
 * are a bounded description of memory and drop what does not fit -- so the
 * scan jumps to the next listed range rather than stepping page by page
 * through a hole.
 */


/*
 * Carve storage for the page array out of physical memory and hand it to the
 * machine-independent layer.
 *
 * It has to be carved rather than allocated: the allocator cannot run until
 * this exists, and this cannot exist until there is somewhere to put it. Both
 * references bootstrap the same way.
 *
 * The old limit was the first 16 MB, which meant a machine with 4 GB got no
 * descriptors at all -- silently, and the allocator then ran without any of
 * the per-page state it thought it had. The real constraint is the higher-half
 * direct map, which boot.S builds for the first gigabyte (PDPT1[510] -> PD0):
 * above that, X86_64_PHYS_TO_VIRT does not name mapped memory. That is a
 * separate limit worth lifting on its own, and it is not this one.
 */
#define PMM_DIRECT_MAP_LIMIT 0x40000000ULL   /* 1 GiB, per boot.S */

static void pmm_setup_page_descs(uint64_t memory_start, uint64_t memory_end,
                                 uint64_t bitmap_phys,
                                 uint64_t bitmap_size)
{
    uint64_t total_pages = (memory_end - memory_start) / PAGE_SIZE;
    uint64_t desc_size = (uint64_t)vm_page_array_bytes(memory_start, memory_end);
    /* Where the page bitmap used to live. It is gone -- the buddy answers
     * "is this page free" on its own -- so this reclaims the space rather
     * than starting after it. */
    (void)bitmap_size;
    uint64_t desc_phys = (bitmap_phys + 7) & ~7ULL;

    if (desc_size == 0 || desc_phys + desc_size > PMM_DIRECT_MAP_LIMIT) {
        /*
         * Fatal, where the old code carried on.
         *
         * It could carry on because reference counts lived in a side table
         * that worked whether or not the descriptors existed. They live in
         * this array now, so without it nothing counts references, nothing is
         * ever freed, and the machine dies of a leak somewhere else entirely.
         * Stopping here says which thing was missing.
         *
         * At eight bytes per page the array is memory/512, so this is
         * reachable somewhere past a couple of hundred gigabytes -- and then
         * the thing to fix is the size of the direct map, not this check.
         */
        panicf("pmm: page array needs %llu KiB at %llx, past the %llu MiB "
               "direct map",
               (unsigned long long)(desc_size / 1024u),
               (unsigned long long)desc_phys,
               (unsigned long long)(PMM_DIRECT_MAP_LIMIT / (1024u * 1024u)));
    }

    void* kva = X86_64_PHYS_TO_VIRT(desc_phys);
    vm_page_init(memory_start, memory_end, kva);

    pmm_state.pages = (vm_page_t*)kva;
    pmm_state.pages_count = total_pages;

    for (uint64_t i = 0; i < total_pages; i++) {
        pmm_state.pages[i].zone =
            (uint8_t)pmm_zone_for_addr(memory_start + (i * PAGE_SIZE));
    }

    klog("pmm", "%llu pages managed, %llu KiB of page state at %llx\n",
         (unsigned long long)total_pages,
         (unsigned long long)(desc_size / 1024u),
         (unsigned long long)desc_phys);
}

/*
 * Hand a range to the allocator.
 *
 * Only the pages that were not already free are handed over, and they go in
 * runs: vm_phys_free_run() decomposes a run into naturally aligned blocks, so
 * a megabyte costs the handful of blocks it decomposes into rather than a
 * page at a time.
 */
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

    uint64_t run_start = 0;
    uint64_t run_pages = 0;

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        if (!pmm_page_is_free(addr)) {
            if (run_pages == 0) {
                run_start = addr;
            }
            run_pages++;
            pmm_state.free_pages++;
            pmm_state.used_pages--;
            pmm_zone_t zone = pmm_zone_for_addr(addr);
            pmm_state.zones[zone].free_pages++;
            if (pmm_state.zones[zone].used_pages > 0) {
                pmm_state.zones[zone].used_pages--;
            }
        } else if (run_pages > 0) {
            vm_phys_free_run(run_start, run_pages);
            run_pages = 0;
        }
    }
    if (run_pages > 0) {
        vm_phys_free_run(run_start, run_pages);
    }
}

/*
 * Take a range out of the allocator's hands.
 *
 * A buddy has no natural way to reserve an arbitrary range -- it deals in
 * aligned blocks -- so each page is removed by splitting whatever block
 * contains it, which is what vm_phys_unfree() is for. Bounded per page, and
 * proportional to the range rather than to the size of memory.
 */
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

    /*
     * Accounted per zone, so the range is split at the zone boundary and each
     * part taken in one call. Blocks, not pages: see vm_phys_unfree_range().
     */
    uint64_t addr = start;
    while (addr < end) {
        pmm_zone_t zone = pmm_zone_for_addr(addr);
        uint64_t chunk_end = end;
        if (zone == PMM_ZONE_LOW && end > PMM_LOW_ZONE_LIMIT) {
            chunk_end = PMM_LOW_ZONE_LIMIT;
        }

        uint64_t took = vm_phys_unfree_range(addr, (chunk_end - addr) / PAGE_SIZE);
        pmm_state.free_pages -= took;
        pmm_state.used_pages += took;
        if (pmm_state.zones[zone].free_pages >= took) {
            pmm_state.zones[zone].free_pages -= took;
            pmm_state.zones[zone].used_pages += took;
        }
        addr = chunk_end;
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
/*
 * Per-zone totals, reported once because until now they were never maintained.
 *
 * Every update to them sat behind "if (index < pmm_state.pages_count)", and on
 * this machine the descriptor array was placed above 16 MB and therefore never
 * created -- so the guard was false for the whole life of the system and the
 * zone counters stayed at zero. The array exists now, so the counters mean
 * something; printing them is how we find out whether they mean the right
 * thing.
 */
static void pmm_report_zones(void)
{
    static const char* const names[PMM_ZONE_COUNT] = { "low", "normal", "mmio" };
    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        if (pmm_state.zones[z].total_pages == 0) {
            continue;
        }
        uint64_t blocks = 0;
        uint32_t largest = 0;
        for (uint32_t o = 0; o < VM_NFREEORDER; o++) {
            uint64_t n = vm_phys_free_blocks((uint8_t)z, o);
            blocks += n;
            if (n) {
                largest = o;
            }
        }
        klog("pmm", "zone %-6s total=%llu free=%llu used=%llu "
             "blocks=%llu largest=%lluKiB\n",
             names[z],
             (unsigned long long)pmm_state.zones[z].total_pages,
             (unsigned long long)pmm_state.zones[z].free_pages,
             (unsigned long long)pmm_state.zones[z].used_pages,
             (unsigned long long)blocks,
             (unsigned long long)(((1ULL << largest) * PAGE_SIZE) / 1024u));
    }
}

/*
 * Fallback initialisation, for a bootloader that supplies no memory map.
 *
 * Everything between memory_start and memory_end is assumed usable, which is
 * exactly as optimistic as it sounds and is why it is the fallback. It used to
 * be four times this length, most of it clearing a bitmap and narrating the
 * clearing; the bitmap is gone and the narration went with it.
 */
int pmm_init(uint64_t memory_start, uint64_t memory_end, void* bitmap_virt)
{
    if (!bitmap_virt || memory_end <= memory_start) {
        return RDNX_E_INVALID;
    }

    memory_start = (memory_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    memory_end = memory_end & ~(PAGE_SIZE - 1);

    uint64_t total_pages = (memory_end - memory_start) / PAGE_SIZE;
    pmm_state.total_pages = total_pages;
    pmm_state.free_pages = 0;
    pmm_state.used_pages = total_pages;
    pmm_state.memory_start = memory_start;
    pmm_state.memory_end = memory_end;

    pmm_regions_clear();
    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        pmm_state.zones[z].total_pages = 0;
        pmm_state.zones[z].free_pages = 0;
        pmm_state.zones[z].used_pages = 0;
    }

    vm_phys_init();
    pmm_setup_page_descs(memory_start, memory_end,
                         X86_64_VIRT_TO_PHYS(bitmap_virt), 0);

    for (uint64_t i = 0; i < pmm_state.pages_count; i++) {
        pmm_state.zones[pmm_state.pages[i].zone].total_pages++;
        pmm_state.zones[pmm_state.pages[i].zone].used_pages++;
    }

    pmm_mark_range_free(memory_start, memory_end);

    /* Whatever the page array occupies is not memory to hand out. */
    uint64_t desc_phys = X86_64_VIRT_TO_PHYS(bitmap_virt);
    uint64_t desc_size = (uint64_t)vm_page_array_bytes(memory_start, memory_end);
    pmm_mark_range_used(desc_phys, desc_phys + desc_size);
    pmm_add_region(pmm_state.reserved_regions, &pmm_state.reserved_count,
                   desc_phys, desc_size);

    pmm_add_region(pmm_state.usable_regions, &pmm_state.usable_count,
                   memory_start, memory_end - memory_start);

    pmm_report_zones();
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
    (void)bitmap_virt;
    pmm_state.memory_start = memory_start;
    pmm_state.memory_end = memory_end;
    /* Everything starts in use: a vm_page is created with no free block
     * covering it, so the available ranges below are the only thing that puts
     * memory into the allocator. */
    vm_phys_init();

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
            pmm_state.pages[i].queue = VM_PQ_NONE;
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

    /* Keep the page array reserved. */
    if (pmm_state.pages_count > 0) {
        uint64_t desc_size = (uint64_t)vm_page_array_bytes(pmm_state.memory_start, pmm_state.memory_end);
        uint64_t desc_phys = (bitmap_phys + 7) & ~7ULL;
        pmm_mark_range_used(desc_phys, desc_phys + desc_size);
        pmm_add_region(pmm_state.reserved_regions, &pmm_state.reserved_count,
                       desc_phys, desc_size);
    }

    pmm_report_zones();
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
/*
 * The physical page allocator, under a lock at last.
 *
 * It had none, across eleven hundred lines, which held only while one thread
 * of control existed. Two processors in the bitmap at once hand the same page
 * to two callers -- and when one of them uses it as a page table and the
 * other as data, the processor starts reporting reserved-bit violations on
 * user memory, which is exactly the fault this was traced back from.
 *
 * irqsave rather than the plain variant: this sits under kmalloc, which is
 * reachable from an interrupt handler, so masking is required and not merely
 * tidy. The sections are a bitmap scan, short by construction.
 */
static spinlock_t pmm_spin;

/* Forward declarations: the convenience wrappers below call these directly
 * rather than the public entry points, which would take the lock twice. */
static uint64_t pmm_alloc_page_in_zone_locked(pmm_zone_t zone);
static uint64_t pmm_alloc_pages_in_zone_locked(pmm_zone_t zone, uint32_t count);
static void pmm_free_page_locked(uint64_t phys);
static void pmm_free_pages_locked(uint64_t phys, uint32_t count);

static uint64_t pmm_alloc_page_locked(void){
    uint64_t phys = pmm_alloc_page_in_zone_locked(PMM_ZONE_NORMAL);
    if (phys) {
        return phys;
    }
    return pmm_alloc_page_in_zone_locked(PMM_ZONE_LOW);
}

uint64_t pmm_alloc_page(void)
{
    uint64_t _f = spinlock_lock_irqsave(&pmm_spin);
    uint64_t _r = pmm_alloc_page_locked();
    spinlock_unlock_irqrestore(&pmm_spin, _f);
    return _r;
}



static uint64_t pmm_alloc_page_in_zone_locked(pmm_zone_t zone){
    return pmm_alloc_pages_in_zone_locked(zone, 1);
}

uint64_t pmm_alloc_page_in_zone(pmm_zone_t zone)
{
    uint64_t _f = spinlock_lock_irqsave(&pmm_spin);
    uint64_t _r = pmm_alloc_page_in_zone_locked(zone);
    spinlock_unlock_irqrestore(&pmm_spin, _f);
    return _r;
}



/**
 * @function pmm_free_page
 * @brief Free a single physical page
 * 
 * @param phys Physical address of page to free
 * 
 * @note The address must be page-aligned and within managed range.
 */
static void pmm_free_page_locked(uint64_t phys){
    pmm_free_pages_locked(phys, 1);
}

void pmm_free_page(uint64_t phys)
{
    uint64_t _f = spinlock_lock_irqsave(&pmm_spin);
    pmm_free_page_locked(phys);
    spinlock_unlock_irqrestore(&pmm_spin, _f);
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
static uint64_t pmm_alloc_pages_locked(uint32_t count){
    uint64_t phys = pmm_alloc_pages_in_zone_locked(PMM_ZONE_NORMAL, count);
    if (phys) {
        return phys;
    }
    return pmm_alloc_pages_in_zone_locked(PMM_ZONE_LOW, count);
}

uint64_t pmm_alloc_pages(uint32_t count)
{
    uint64_t _f = spinlock_lock_irqsave(&pmm_spin);
    uint64_t _r = pmm_alloc_pages_locked(count);
    spinlock_unlock_irqrestore(&pmm_spin, _f);
    return _r;
}



/*
 * The whole point of this stage lives in these few lines.
 *
 * What it replaces walked a 128-entry range array and, when that no longer
 * described enough memory, scanned every page of the bitmap and then rebuilt
 * every list from it -- twice O(all memory), for one page, with interrupts
 * masked. Here the cost is at most VM_NFREEORDER splits, whatever the state of
 * memory, which is the difference between an allocator with a worst case and
 * one without.
 */
static uint64_t pmm_alloc_pages_in_zone_locked(pmm_zone_t zone, uint32_t count){
    if (count == 0 || zone >= PMM_ZONE_COUNT) {
        return 0;
    }

    uint64_t phys = vm_phys_alloc((uint8_t)zone, count);
    if (!phys) {
        TRACE_EVENT("oom: pmm_alloc_pages");
        memory_oom_inc_pmm();
        return 0;
    }

    pmm_state.free_pages -= count;
    pmm_state.used_pages += count;
    if (pmm_state.zones[zone].free_pages >= count) {
        pmm_state.zones[zone].free_pages -= count;
        pmm_state.zones[zone].used_pages += count;
    }

    for (uint32_t i = 0; i < count; i++) {
        pmm_zero_page(phys + ((uint64_t)i * PAGE_SIZE));
    }
    return phys;
}

uint64_t pmm_alloc_pages_in_zone(pmm_zone_t zone, uint32_t count)
{
    uint64_t _f = spinlock_lock_irqsave(&pmm_spin);
    uint64_t _r = pmm_alloc_pages_in_zone_locked(zone, count);
    spinlock_unlock_irqrestore(&pmm_spin, _f);
    return _r;
}



static void pmm_reserve_range_locked(uint64_t start, uint64_t end){
    if (end <= start) {
        return;
    }
    pmm_mark_range_used(start, end);
    pmm_add_region(pmm_state.reserved_regions, &pmm_state.reserved_count,
                   start, end - start);
}

void pmm_reserve_range(uint64_t start, uint64_t end)
{
    uint64_t _f = spinlock_lock_irqsave(&pmm_spin);
    pmm_reserve_range_locked(start, end);
    spinlock_unlock_irqrestore(&pmm_spin, _f);
}



static void pmm_release_range_locked(uint64_t start, uint64_t end){
    if (end <= start) {
        return;
    }
    pmm_mark_range_free(start, end);
}

void pmm_release_range(uint64_t start, uint64_t end)
{
    uint64_t _f = spinlock_lock_irqsave(&pmm_spin);
    pmm_release_range_locked(start, end);
    spinlock_unlock_irqrestore(&pmm_spin, _f);
}



/**
 * @function pmm_free_pages
 * @brief Free multiple physical pages
 * 
 * @param phys Physical address of first page
 * @param count Number of pages to free
 */
static void pmm_free_pages_locked(uint64_t phys, uint32_t count){
    if (count == 0 || (phys & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    if (phys < pmm_state.memory_start ||
        phys + ((uint64_t)count * PAGE_SIZE) > pmm_state.memory_end) {
        return;
    }

    /* Freeing what is already free would corrupt the buddy lists, so each page
     * is checked. The check is the same bounded search the allocator uses, not
     * a second record of the same fact. */
    uint64_t run_start = 0;
    uint64_t run_pages = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t addr = phys + ((uint64_t)i * PAGE_SIZE);
        if (pmm_page_is_free(addr)) {
            DEBUG_WARN("pmm: double free of page %llx", (unsigned long long)addr);
            if (run_pages > 0) {
                vm_phys_free_run(run_start, run_pages);
                run_pages = 0;
            }
            continue;
        }
        if (run_pages == 0) {
            run_start = addr;
        }
        run_pages++;

        pmm_state.free_pages++;
        pmm_state.used_pages--;
        pmm_zone_t zone = pmm_zone_for_addr(addr);
        pmm_state.zones[zone].free_pages++;
        if (pmm_state.zones[zone].used_pages > 0) {
            pmm_state.zones[zone].used_pages--;
        }
    }
    if (run_pages > 0) {
        vm_phys_free_run(run_start, run_pages);
    }
}

void pmm_free_pages(uint64_t phys, uint32_t count)
{
    uint64_t _f = spinlock_lock_irqsave(&pmm_spin);
    pmm_free_pages_locked(phys, count);
    spinlock_unlock_irqrestore(&pmm_spin, _f);
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

/*
 * Free memory as the allocator actually holds it: one entry per free block,
 * largest first. It used to report the 128-entry range array, which silently
 * described only part of memory once it overflowed.
 */
int pmm_get_free_regions(pmm_zone_t zone, pmm_region_t* out, uint32_t max,
                         uint32_t* out_count)
{
    if (!out || !out_count || zone >= PMM_ZONE_COUNT) {
        return RDNX_E_INVALID;
    }

    uint32_t n = 0;
    uint64_t f = spinlock_lock_irqsave(&pmm_spin);
    for (int order = VM_NFREEORDER - 1; order >= 0 && n < max; order--) {
        for (uint32_t i = vm_phys_block_first((uint8_t)zone, (uint32_t)order);
             i != VM_PAGE_NIL && n < max;
             i = vm_phys_block_next(i)) {
            vm_page_t* m = vm_page_at_index(i);
            if (!m) {
                break;
            }
            out[n].base = vm_page_phys(m);
            out[n].length = (1ULL << order) * PAGE_SIZE;
            n++;
        }
    }
    spinlock_unlock_irqrestore(&pmm_spin, f);

    *out_count = n;
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
