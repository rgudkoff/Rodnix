/**
 * @file x86_64/memory.c
 * @brief Реализация управления памятью для x86_64
 */

#include "../../core/memory.h"
#include "types.h"
#include "config.h"
#include <stddef.h>
#include <stdbool.h>

/* Temporary functions for compatibility (will be replaced) */
/* These should be implemented in kernel/common/memory.c or similar */
static uint64_t pmm_alloc_page_impl(void) {
    /* TODO: Implement actual PMM allocation */
    return 0;
}

static void pmm_free_page_impl(uint64_t phys) {
    /* TODO: Implement actual PMM deallocation */
    (void)phys;
}

static int paging_map_page_impl(uint64_t virt, uint64_t phys, uint64_t flags) {
    /* TODO: Implement actual page mapping */
    (void)virt;
    (void)phys;
    (void)flags;
    return 0;
}

static uint64_t paging_get_physical_impl(uint64_t virt) {
    /* TODO: Implement actual physical address lookup */
    (void)virt;
    return 0;
}

static void* vmm_alloc_page_impl(uint64_t flags) {
    /* TODO: Implement actual VMM allocation */
    (void)flags;
    return NULL;
}

static void vmm_free_page_impl(void* virt) {
    /* TODO: Implement actual VMM deallocation */
    (void)virt;
}
extern uint32_t pmm_get_total_pages(void);
extern uint32_t pmm_get_free_pages(void);
extern uint32_t pmm_get_used_pages(void);
extern uint64_t vmm_get_total_memory(void);
extern uint64_t vmm_get_free_memory(void);
extern uint64_t vmm_get_used_memory(void);

int memory_init(void)
{
    /* Инициализация физического менеджера памяти */
    /* Предполагаем, что pmm_init уже вызван */
    return 0;
}

int paging_init(void)
{
    /* Инициализация пейджинга x86_64 */
    /* TODO: Реализовать полную инициализацию */
    return 0;
}

int page_map(uint64_t virt, uint64_t phys, uint64_t flags, page_type_t type)
{
    uint64_t x86_flags = 0;
    
    /* Преобразование флагов */
    if (flags & PAGE_FLAG_PRESENT)  x86_flags |= 0x01;
    if (flags & PAGE_FLAG_WRITABLE) x86_flags |= 0x02;
    if (flags & PAGE_FLAG_USER)     x86_flags |= 0x04;
    if (flags & PAGE_FLAG_NOCACHE)  x86_flags |= 0x10;
    if (flags & PAGE_FLAG_GLOBAL)   x86_flags |= 0x100;
    if (!(flags & PAGE_FLAG_EXECUTE)) x86_flags |= 0x8000000000000000ULL; /* NX bit */
    
    /* Определение размера страницы */
    if (type == PAGE_TYPE_2MB) {
        /* TODO: Реализовать поддержку 2MB страниц */
        /* Пока используем обычные 4KB страницы */
    } else if (type == PAGE_TYPE_1GB) {
        /* TODO: Реализовать поддержку 1GB страниц */
        /* Пока используем обычные 4KB страницы */
    }
    
    /* Map page */
    return paging_map_page_impl(virt, phys, x86_flags);
}

int page_unmap(uint64_t virt)
{
    /* TODO: Реализовать удаление отображения */
    (void)virt;
    return -1;
}

uint64_t page_get_physical(uint64_t virt)
{
    return paging_get_physical_impl(virt);
}

uint64_t page_get_virtual(uint64_t phys)
{
    /* Для x86_64 с identity mapping */
    if (phys < 0x400000) {
        return phys;  /* Identity mapped */
    }
    /* Или через high-half mapping */
    return phys + X86_64_KERNEL_VIRT_BASE;
}

uint64_t pmm_alloc_page(void)
{
    return pmm_alloc_page_impl();
}

void pmm_free_page(uint64_t phys)
{
    pmm_free_page_impl(phys);
}

uint64_t pmm_alloc_pages(uint32_t count)
{
    uint64_t first_page = pmm_alloc_page();
    if (!first_page) {
        return 0;
    }
    
    for (uint32_t i = 1; i < count; i++) {
        uint64_t page = pmm_alloc_page();
        if (!page) {
            /* Освобождаем уже выделенные страницы */
            for (uint32_t j = 0; j < i; j++) {
                pmm_free_page(first_page + j * PAGE_SIZE);
            }
            return 0;
        }
    }
    
    return first_page;
}

void pmm_free_pages(uint64_t phys, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        pmm_free_page(phys + i * PAGE_SIZE);
    }
}

void* vmm_alloc_page(uint64_t flags)
{
    return vmm_alloc_page_impl(flags);
}

void vmm_free_page(void* virt)
{
    vmm_free_page_impl(virt);
}

void* vmm_alloc_pages(uint32_t count, uint64_t flags)
{
    void* first_page = vmm_alloc_page(flags);
    if (!first_page) {
        return NULL;
    }
    
    for (uint32_t i = 1; i < count; i++) {
        void* page = vmm_alloc_page(flags);
        if (!page) {
            /* Освобождаем уже выделенные страницы */
            for (uint32_t j = 0; j < i; j++) {
                vmm_free_page((void*)((uintptr_t)first_page + j * PAGE_SIZE));
            }
            return NULL;
        }
    }
    
    return first_page;
}

void vmm_free_pages(void* virt, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        vmm_free_page((void*)((uintptr_t)virt + i * PAGE_SIZE));
    }
}

int memory_get_info(memory_info_t* info)
{
    if (!info) {
        return -1;
    }
    
    /* TODO: Get actual memory statistics */
    info->total_physical = 0;
    info->free_physical = 0;
    info->used_physical = 0;
    info->total_virtual = 0;
    info->free_virtual = 0;
    info->used_virtual = 0;
    
    return 0;
}

