/**
 * @file pmm.h
 * @brief Physical Memory Manager (PMM) interface
 */

#ifndef _RODNIX_ARCH_X86_64_PMM_H
#define _RODNIX_ARCH_X86_64_PMM_H

#include <stdint.h>

/* Initialize PMM */
int pmm_init(uint64_t memory_start, uint64_t memory_end, void* bitmap_virt);

/* Allocate/free pages */
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys);
uint64_t pmm_alloc_pages(uint32_t count);
void pmm_free_pages(uint64_t phys, uint32_t count);

/* Statistics */
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_used_pages(void);

#endif /* _RODNIX_ARCH_X86_64_PMM_H */

