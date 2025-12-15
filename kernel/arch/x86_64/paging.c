/**
 * @file paging.c
 * @brief x86_64 paging implementation
 * 
 * This module implements x86_64 page table management with support for
 * 4KB, 2MB, and 1GB pages. It follows VM architecture principles:
 * - Hierarchical page table structure (PML4 -> PDPT -> PD -> PT)
 * - Support for large pages (2MB, 1GB) for performance
 * - Efficient page table allocation and management
 * - TLB management considerations
 * 
 * @note This implementation is adapted for RodNIX.
 */

#include "types.h"
#include "config.h"
#include "pmm.h"
#include "../../../include/debug.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Ensure X86_64_KERNEL_VIRT_BASE is defined */
#ifndef X86_64_KERNEL_VIRT_BASE
#define X86_64_KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL
#endif

/* ============================================================================
 * Page Table Constants
 * ============================================================================ */

/* Page table entry flags (x86_64) */
#define PTE_PRESENT     0x001   /* Present bit */
#define PTE_RW          0x002   /* Read/Write bit */
#define PTE_USER        0x004   /* User/Supervisor bit */
#define PTE_PWT         0x008   /* Page Write Through */
#define PTE_PCD         0x010   /* Page Cache Disable */
#define PTE_ACCESSED    0x020   /* Accessed bit */
#define PTE_DIRTY       0x040   /* Dirty bit (for PT entries) */
#define PTE_PAT         0x080   /* Page Attribute Table */
#define PTE_GLOBAL      0x100   /* Global page (TLB) */
#define PTE_SIZE_2MB    0x080   /* Page size bit (2MB page) */
#define PTE_SIZE_1GB    0x080   /* Page size bit (1GB page) */
#define PTE_NX          0x8000000000000000ULL  /* No Execute bit */

/* Page table structure sizes */
#define PML4_ENTRIES    512     /* 512 entries per PML4 */
#define PDPT_ENTRIES    512     /* 512 entries per PDPT */
#define PD_ENTRIES      512     /* 512 entries per PD */
#define PT_ENTRIES      512     /* 512 entries per PT */

/* Address masks and shifts */
#define PML4_SHIFT      39
#define PDPT_SHIFT      30
#define PD_SHIFT        21
#define PT_SHIFT        12
#define PAGE_OFFSET_MASK 0xFFF

/* ============================================================================
 * Current Page Table
 * ============================================================================ */

/* Current PML4 (CR3 register value) */
static uint64_t current_pml4_phys = 0;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @function paging_get_pml4_index
 * @brief Extract PML4 index from virtual address
 * 
 * @param virt Virtual address
 * @return PML4 index (0-511)
 */
static inline uint64_t paging_get_pml4_index(uint64_t virt)
{
    return (virt >> PML4_SHIFT) & 0x1FF;
}

/**
 * @function paging_get_pdpt_index
 * @brief Extract PDPT index from virtual address
 * 
 * @param virt Virtual address
 * @return PDPT index (0-511)
 */
static inline uint64_t paging_get_pdpt_index(uint64_t virt)
{
    return (virt >> PDPT_SHIFT) & 0x1FF;
}

/**
 * @function paging_get_pd_index
 * @brief Extract PD index from virtual address
 * 
 * @param virt Virtual address
 * @return PD index (0-511)
 */
static inline uint64_t paging_get_pd_index(uint64_t virt)
{
    return (virt >> PD_SHIFT) & 0x1FF;
}

/**
 * @function paging_get_pt_index
 * @brief Extract PT index from virtual address
 * 
 * @param virt Virtual address
 * @return PT index (0-511)
 */
static inline uint64_t paging_get_pt_index(uint64_t virt)
{
    return (virt >> PT_SHIFT) & 0x1FF;
}

/**
 * @function paging_get_page_offset
 * @brief Extract page offset from virtual address
 * 
 * @param virt Virtual address
 * @return Page offset (0-4095)
 */
static inline uint64_t paging_get_page_offset(uint64_t virt)
{
    return virt & PAGE_OFFSET_MASK;
}

/**
 * @function paging_alloc_page_table
 * @brief Allocate a physical page for a page table
 * 
 * This function allocates a physical page for use as a page table structure
 * (PML4, PDPT, PD, or PT). The page is zeroed to ensure clean state.
 * 
 * @return Physical address of allocated page, or 0 on failure
 * 
 * @note Page tables are allocated from a dedicated zone.
 *       For now, we use the general PMM allocator.
 */
static uint64_t paging_alloc_page_table(void)
{
    uint64_t phys = pmm_alloc_page();
    if (!phys) {
        return 0;
    }
    
    /* Zero the page table (ensure clean page tables) */
    void* virt = (void*)(phys + X86_64_KERNEL_VIRT_BASE);
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        ((uint64_t*)virt)[i] = 0;
    }
    
    return phys;
}

/**
 * @function paging_get_pml4
 * @brief Get current PML4 virtual address
 * 
 * @return Virtual address of current PML4
 */
static uint64_t* paging_get_pml4(void)
{
    if (!current_pml4_phys) {
        return NULL;
    }
    return (uint64_t*)(current_pml4_phys + X86_64_KERNEL_VIRT_BASE);
}

/**
 * @function paging_get_pdpt
 * @brief Get PDPT virtual address from PML4 entry
 * 
 * @param pml4_entry PML4 entry value
 * @return Virtual address of PDPT, or NULL if not present
 */
static uint64_t* paging_get_pdpt(uint64_t pml4_entry)
{
    if (!(pml4_entry & PTE_PRESENT)) {
        return NULL;
    }
    uint64_t pdpt_phys = pml4_entry & ~0xFFF;
    return (uint64_t*)(pdpt_phys + X86_64_KERNEL_VIRT_BASE);
}

/**
 * @function paging_get_pd
 * @brief Get PD virtual address from PDPT entry
 * 
 * @param pdpt_entry PDPT entry value
 * @return Virtual address of PD, or NULL if not present
 */
static uint64_t* paging_get_pd(uint64_t pdpt_entry)
{
    if (!(pdpt_entry & PTE_PRESENT)) {
        return NULL;
    }
    uint64_t pd_phys = pdpt_entry & ~0xFFF;
    return (uint64_t*)(pd_phys + X86_64_KERNEL_VIRT_BASE);
}

/**
 * @function paging_get_pt
 * @brief Get PT virtual address from PD entry
 * 
 * @param pd_entry PD entry value
 * @return Virtual address of PT, or NULL if not present
 */
static uint64_t* paging_get_pt(uint64_t pd_entry)
{
    if (!(pd_entry & PTE_PRESENT)) {
        return NULL;
    }
    /* Check if this is a 2MB page */
    if (pd_entry & PTE_SIZE_2MB) {
        return NULL; /* This is a 2MB page, not a PT */
    }
    uint64_t pt_phys = pd_entry & ~0xFFF;
    return (uint64_t*)(pt_phys + X86_64_KERNEL_VIRT_BASE);
}

/**
 * @function paging_flush_tlb
 * @brief Flush TLB for a virtual address
 * 
 * @param virt Virtual address to flush (can be NULL for full flush)
 */
static void paging_flush_tlb(void* virt)
{
    if (virt) {
        /* Invalidate specific page */
        __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    } else {
        /* Full TLB flush */
        uint64_t cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
    }
}

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function paging_init
 * @brief Initialize paging subsystem
 * 
 * This function initializes the paging subsystem. It uses the PML4
 * that was set up during boot (in boot.S) as the initial page table.
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note The initial page tables are set up in boot.S with identity mapping.
 *       This function extends that setup for kernel use.
 */
int paging_init(void)
{
    /* Get current CR3 (PML4 physical address) */
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current_pml4_phys));
    
    if (!current_pml4_phys) {
        return -1;
    }
    return 0;
}

/**
 * @function paging_map_page_4kb
 * @brief Map a 4KB page
 * 
 * @param virt Virtual address (must be 4KB aligned)
 * @param phys Physical address (must be 4KB aligned)
 * @param flags Page flags (PTE_* constants)
 * 
 * @return 0 on success, -1 on failure
 */
int paging_map_page_4kb(uint64_t virt, uint64_t phys, uint64_t flags)
{
    if ((virt & PAGE_OFFSET_MASK) != 0 || (phys & PAGE_OFFSET_MASK) != 0) {
        return -1; /* Not page-aligned */
    }
    
    uint64_t* pml4 = paging_get_pml4();
    if (!pml4) {
        return -1;
    }
    
    /* Get indices */
    uint64_t pml4_idx = paging_get_pml4_index(virt);
    uint64_t pdpt_idx = paging_get_pdpt_index(virt);
    uint64_t pd_idx = paging_get_pd_index(virt);
    uint64_t pt_idx = paging_get_pt_index(virt);
    
    /* Get or create PML4 entry */
    uint64_t pml4_entry = pml4[pml4_idx];
    uint64_t* pdpt;
    
    if (!(pml4_entry & PTE_PRESENT)) {
        /* Allocate new PDPT */
        uint64_t pdpt_phys = paging_alloc_page_table();
        if (!pdpt_phys) {
            return -1;
        }
        pml4_entry = pdpt_phys | PTE_PRESENT | PTE_RW;
        pml4[pml4_idx] = pml4_entry;
        pdpt = (uint64_t*)(pdpt_phys + X86_64_KERNEL_VIRT_BASE);
    } else {
        pdpt = paging_get_pdpt(pml4_entry);
    }
    
    /* Get or create PDPT entry */
    uint64_t pdpt_entry = pdpt[pdpt_idx];
    uint64_t* pd;
    
    if (!(pdpt_entry & PTE_PRESENT)) {
        /* Allocate new PD */
        uint64_t pd_phys = paging_alloc_page_table();
        if (!pd_phys) {
            return -1;
        }
        pdpt_entry = pd_phys | PTE_PRESENT | PTE_RW;
        pdpt[pdpt_idx] = pdpt_entry;
        pd = (uint64_t*)(pd_phys + X86_64_KERNEL_VIRT_BASE);
    } else {
        pd = paging_get_pd(pdpt_entry);
    }
    
    /* Get or create PD entry */
    uint64_t pd_entry = pd[pd_idx];
    uint64_t* pt;
    
    if (!(pd_entry & PTE_PRESENT)) {
        /* Allocate new PT */
        uint64_t pt_phys = paging_alloc_page_table();
        if (!pt_phys) {
            return -1;
        }
        pd_entry = pt_phys | PTE_PRESENT | PTE_RW;
        pd[pd_idx] = pd_entry;
        pt = (uint64_t*)(pt_phys + X86_64_KERNEL_VIRT_BASE);
    } else {
        /* Check if this is a 2MB page */
        if (pd_entry & PTE_SIZE_2MB) {
            return -1; /* Cannot map 4KB page where 2MB page exists */
        }
        pt = paging_get_pt(pd_entry);
    }
    
    /* Set PT entry */
    uint64_t pte = phys | flags | PTE_PRESENT;
    pt[pt_idx] = pte;
    
    /* Flush TLB for this page */
    paging_flush_tlb((void*)virt);
    
    return 0;
}

/**
 * @function paging_unmap_page
 * @brief Unmap a page
 * 
 * @param virt Virtual address to unmap
 * 
 * @return 0 on success, -1 on failure
 */
int paging_unmap_page(uint64_t virt)
{
    uint64_t* pml4 = paging_get_pml4();
    if (!pml4) {
        return -1;
    }
    
    /* Get indices */
    uint64_t pml4_idx = paging_get_pml4_index(virt);
    uint64_t pdpt_idx = paging_get_pdpt_index(virt);
    uint64_t pd_idx = paging_get_pd_index(virt);
    uint64_t pt_idx = paging_get_pt_index(virt);
    
    /* Walk page tables */
    uint64_t pml4_entry = pml4[pml4_idx];
    if (!(pml4_entry & PTE_PRESENT)) {
        return -1; /* Not mapped */
    }
    
    uint64_t* pdpt = paging_get_pdpt(pml4_entry);
    uint64_t pdpt_entry = pdpt[pdpt_idx];
    if (!(pdpt_entry & PTE_PRESENT)) {
        return -1;
    }
    
    uint64_t* pd = paging_get_pd(pdpt_entry);
    uint64_t pd_entry = pd[pd_idx];
    if (!(pd_entry & PTE_PRESENT)) {
        return -1;
    }
    
    /* Check if this is a 2MB page */
    if (pd_entry & PTE_SIZE_2MB) {
        /* Unmap 2MB page */
        pd[pd_idx] = 0;
        paging_flush_tlb((void*)virt);
        return 0;
    }
    
    /* Unmap 4KB page */
    uint64_t* pt = paging_get_pt(pd_entry);
    pt[pt_idx] = 0;
    
    /* Flush TLB */
    paging_flush_tlb((void*)virt);
    
    return 0;
}

/**
 * @function paging_get_physical
 * @brief Get physical address for a virtual address
 * 
 * @param virt Virtual address
 * 
 * @return Physical address, or 0 if not mapped
 */
uint64_t paging_get_physical(uint64_t virt)
{
    uint64_t* pml4 = paging_get_pml4();
    if (!pml4) {
        return 0;
    }
    
    /* Get indices */
    uint64_t pml4_idx = paging_get_pml4_index(virt);
    uint64_t pdpt_idx = paging_get_pdpt_index(virt);
    uint64_t pd_idx = paging_get_pd_index(virt);
    uint64_t pt_idx = paging_get_pt_index(virt);
    
    /* Walk page tables */
    uint64_t pml4_entry = pml4[pml4_idx];
    if (!(pml4_entry & PTE_PRESENT)) {
        return 0;
    }
    
    uint64_t* pdpt = paging_get_pdpt(pml4_entry);
    uint64_t pdpt_entry = pdpt[pdpt_idx];
    if (!(pdpt_entry & PTE_PRESENT)) {
        return 0;
    }
    
    uint64_t* pd = paging_get_pd(pdpt_entry);
    uint64_t pd_entry = pd[pd_idx];
    if (!(pd_entry & PTE_PRESENT)) {
        return 0;
    }
    
    /* Check if this is a 2MB page */
    if (pd_entry & PTE_SIZE_2MB) {
        uint64_t phys = (pd_entry & ~0x1FFFFF) | (virt & 0x1FFFFF);
        return phys;
    }
    
    /* Get 4KB page */
    uint64_t* pt = paging_get_pt(pd_entry);
    uint64_t pte = pt[pt_idx];
    if (!(pte & PTE_PRESENT)) {
        return 0;
    }
    
    uint64_t phys = (pte & ~PAGE_OFFSET_MASK) | (virt & PAGE_OFFSET_MASK);
    return phys;
}

/**
 * @function paging_map_page_2mb
 * @brief Map a 2MB page (large page)
 * 
 * @param virt Virtual address (must be 2MB aligned)
 * @param phys Physical address (must be 2MB aligned)
 * @param flags Page flags
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note 2MB pages are more efficient than 4KB pages for large mappings.
 */
int paging_map_page_2mb(uint64_t virt, uint64_t phys, uint64_t flags)
{
    if ((virt & 0x1FFFFF) != 0 || (phys & 0x1FFFFF) != 0) {
        return -1; /* Not 2MB aligned */
    }
    
    uint64_t* pml4 = paging_get_pml4();
    if (!pml4) {
        return -1;
    }
    
    /* Get indices */
    uint64_t pml4_idx = paging_get_pml4_index(virt);
    uint64_t pdpt_idx = paging_get_pdpt_index(virt);
    uint64_t pd_idx = paging_get_pd_index(virt);
    
    /* Get or create PML4 entry */
    uint64_t pml4_entry = pml4[pml4_idx];
    uint64_t* pdpt;
    
    if (!(pml4_entry & PTE_PRESENT)) {
        uint64_t pdpt_phys = paging_alloc_page_table();
        if (!pdpt_phys) {
            return -1;
        }
        pml4_entry = pdpt_phys | PTE_PRESENT | PTE_RW;
        pml4[pml4_idx] = pml4_entry;
        pdpt = (uint64_t*)(pdpt_phys + X86_64_KERNEL_VIRT_BASE);
    } else {
        pdpt = paging_get_pdpt(pml4_entry);
    }
    
    /* Get or create PDPT entry */
    uint64_t pdpt_entry = pdpt[pdpt_idx];
    uint64_t* pd;
    
    if (!(pdpt_entry & PTE_PRESENT)) {
        uint64_t pd_phys = paging_alloc_page_table();
        if (!pd_phys) {
            return -1;
        }
        pdpt_entry = pd_phys | PTE_PRESENT | PTE_RW;
        pdpt[pdpt_idx] = pdpt_entry;
        pd = (uint64_t*)(pd_phys + X86_64_KERNEL_VIRT_BASE);
    } else {
        pd = paging_get_pd(pdpt_entry);
    }
    
    /* Set PD entry as 2MB page */
    uint64_t pd_entry = phys | flags | PTE_PRESENT | PTE_SIZE_2MB;
    pd[pd_idx] = pd_entry;
    
    /* Flush TLB */
    paging_flush_tlb((void*)virt);
    
    return 0;
}

