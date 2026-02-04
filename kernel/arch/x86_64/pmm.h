/**
 * @file pmm.h
 * @brief Physical Memory Manager (PMM) interface
 */

#ifndef _RODNIX_ARCH_X86_64_PMM_H
#define _RODNIX_ARCH_X86_64_PMM_H

#include <stdint.h>

typedef enum {
    PMM_ZONE_LOW = 0,
    PMM_ZONE_NORMAL = 1,
    PMM_ZONE_MMIO = 2,
    PMM_ZONE_COUNT
} pmm_zone_t;

typedef struct {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
} pmm_zone_stats_t;

typedef struct {
    uint64_t base;
    uint64_t length;
} pmm_region_t;

/* Initialize PMM */
int pmm_init(uint64_t memory_start, uint64_t memory_end, void* bitmap_virt);
int pmm_init_from_mmap(uint64_t memory_start, uint64_t memory_end,
                       void* bitmap_virt, uint64_t bitmap_phys,
                       const void* mmap_tag, uint32_t mmap_size, uint32_t entry_size);

/* Allocate/free pages */
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_page_in_zone(pmm_zone_t zone);
void pmm_free_page(uint64_t phys);
uint64_t pmm_alloc_pages(uint32_t count);
uint64_t pmm_alloc_pages_in_zone(pmm_zone_t zone, uint32_t count);
void pmm_free_pages(uint64_t phys, uint32_t count);

/* Statistics */
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_used_pages(void);
int pmm_get_zone_stats(pmm_zone_t zone, pmm_zone_stats_t* out);
int pmm_get_free_regions(pmm_zone_t zone, pmm_region_t* out, uint32_t max,
                         uint32_t* out_count);

#endif /* _RODNIX_ARCH_X86_64_PMM_H */
