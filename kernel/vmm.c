#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/console.h"
#include "../include/common.h"

static uint32_t total_memory = 0;
static uint32_t free_memory = 0;
static uint32_t used_memory = 0;
static uint32_t next_virt_addr = KERNEL_HEAP_START;

/* Инициализация VMM */
int vmm_init(void)
{
    total_memory = pmm_get_total_pages() * PAGE_SIZE;
    free_memory = total_memory;
    used_memory = 0;
    next_virt_addr = KERNEL_HEAP_START;
    
    kputs("[VMM] Initialized: ");
    kprint_dec(total_memory / 1024);
    kputs(" KB virtual memory\n");
    
    return 0;
}

/* Выделение виртуальной памяти */
void* vmm_alloc(uint32_t size, uint32_t flags)
{
    if (size == 0)
        return NULL;
    
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    return vmm_alloc_pages(pages, flags);
}

/* Освобождение виртуальной памяти */
void vmm_free(void* virt, uint32_t size)
{
    if (!virt)
        return;
    
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    vmm_free_pages(virt, pages);
}

/* Выделение страницы */
void* vmm_alloc_page(uint32_t flags)
{
    /* Выделяем физическую страницу */
    uint32_t phys = pmm_alloc_page();
    if (!phys)
        return NULL;
    
    /* Находим свободный виртуальный адрес */
    uint32_t virt = next_virt_addr;
    next_virt_addr += PAGE_SIZE;
    
    /* Отображаем страницу */
    if (paging_map_page(virt, phys, flags) != 0)
    {
        pmm_free_page(phys);
        return NULL;
    }
    
    used_memory += PAGE_SIZE;
    free_memory -= PAGE_SIZE;
    
    return (void*)virt;
}

/* Освобождение страницы */
void vmm_free_page(void* virt)
{
    if (!virt)
        return;
    
    uint32_t virt_addr = (uint32_t)virt;
    uint32_t phys = paging_get_physical(virt_addr);
    
    if (phys)
    {
        paging_unmap_page(virt_addr);
        pmm_free_page(phys);
        
        used_memory -= PAGE_SIZE;
        free_memory += PAGE_SIZE;
    }
}

/* Выделение нескольких страниц */
void* vmm_alloc_pages(uint32_t count, uint32_t flags)
{
    if (count == 0)
        return NULL;
    
    /* Выделяем физические страницы */
    uint32_t phys = pmm_alloc_pages(count);
    if (!phys)
        return NULL;
    
    /* Находим свободный виртуальный адрес */
    uint32_t virt = next_virt_addr;
    next_virt_addr += count * PAGE_SIZE;
    
    /* Отображаем страницы */
    if (paging_map_pages(virt, phys, count, flags) != 0)
    {
        pmm_free_pages(phys, count);
        return NULL;
    }
    
    uint32_t size = count * PAGE_SIZE;
    used_memory += size;
    free_memory -= size;
    
    return (void*)virt;
}

/* Освобождение нескольких страниц */
void vmm_free_pages(void* virt, uint32_t count)
{
    if (!virt || count == 0)
        return;
    
    uint32_t virt_addr = (uint32_t)virt;
    
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t page_virt = virt_addr + i * PAGE_SIZE;
        uint32_t phys = paging_get_physical(page_virt);
        
        if (phys)
        {
            paging_unmap_page(page_virt);
            pmm_free_page(phys);
        }
    }
    
    uint32_t size = count * PAGE_SIZE;
    used_memory -= size;
    free_memory += size;
}

/* Отображение физической памяти в виртуальную */
void* vmm_map_physical(uint32_t phys, uint32_t size, uint32_t flags)
{
    if (size == 0)
        return NULL;
    
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t virt = next_virt_addr;
    next_virt_addr += pages * PAGE_SIZE;
    
    /* Отображаем страницы */
    if (paging_map_pages(virt, phys, pages, flags) != 0)
        return NULL;
    
    return (void*)virt;
}

/* Снятие отображения физической памяти */
void vmm_unmap_physical(void* virt, uint32_t size)
{
    if (!virt)
        return;
    
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t virt_addr = (uint32_t)virt;
    
    for (uint32_t i = 0; i < pages; i++)
    {
        paging_unmap_page(virt_addr + i * PAGE_SIZE);
    }
}

/* Получение информации */
uint32_t vmm_get_total_memory(void)
{
    return total_memory;
}

uint32_t vmm_get_free_memory(void)
{
    return free_memory;
}

uint32_t vmm_get_used_memory(void)
{
    return used_memory;
}

