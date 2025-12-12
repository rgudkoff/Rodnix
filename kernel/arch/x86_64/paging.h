/**
 * @file paging.h
 * @brief x86_64 paging interface
 */

#ifndef _RODNIX_ARCH_X86_64_PAGING_H
#define _RODNIX_ARCH_X86_64_PAGING_H

#include <stdint.h>

/* Initialize paging */
int paging_init(void);

/* Map pages */
int paging_map_page_4kb(uint64_t virt, uint64_t phys, uint64_t flags);
int paging_map_page_2mb(uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap page */
int paging_unmap_page(uint64_t virt);

/* Get physical address */
uint64_t paging_get_physical(uint64_t virt);

/* Page table entry flags */
#define PTE_PRESENT     0x001
#define PTE_RW          0x002
#define PTE_USER        0x004
#define PTE_PWT         0x008
#define PTE_PCD         0x010
#define PTE_ACCESSED    0x020
#define PTE_DIRTY       0x040
#define PTE_PAT         0x080
#define PTE_GLOBAL      0x100
#define PTE_NX          0x8000000000000000ULL

#endif /* _RODNIX_ARCH_X86_64_PAGING_H */

