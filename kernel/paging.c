#include "../include/paging.h"
#include "../include/pmm.h"
#include "../include/console.h"
#include "../include/common.h"

/* Текущий page directory */
static uint32_t* current_page_dir = NULL;

/* Вспомогательная функция для получения PTE */
static pte_t* get_pte(uint32_t* page_dir, uint32_t virt, int create)
{
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t table_index = PAGE_TABLE_INDEX(virt);
    
    pde_t* pde = (pde_t*)&page_dir[dir_index];
    
    /* Если page table не существует, создаем её */
    if (!pde->present)
    {
        if (!create)
            return NULL;
        
        /* Выделяем страницу для page table */
        uint32_t table_frame = pmm_alloc_page();
        if (!table_frame)
            return NULL;
        
        /* Очищаем page table */
        uint32_t* table = (uint32_t*)(table_frame);
        for (int i = 0; i < 1024; i++)
            table[i] = 0;
        
        /* Устанавливаем PDE */
        pde->present = 1;
        pde->rw = 1;
        pde->user = 0;
        pde->frame = PAGE_FRAME(table_frame);
    }
    
    /* Получаем адрес page table */
    uint32_t* page_table = (uint32_t*)(FRAME_ADDR(pde->frame));
    return (pte_t*)&page_table[table_index];
}

/* Инициализация paging */
int paging_init(void)
{
    /* Выделяем страницу для page directory */
    uint32_t dir_frame = pmm_alloc_page();
    if (!dir_frame)
    {
        kputs("[PAGING] Error: Failed to allocate page directory\n");
        return -1;
    }
    
    current_page_dir = (uint32_t*)dir_frame;
    
    /* Очищаем page directory */
    for (int i = 0; i < 1024; i++)
        current_page_dir[i] = 0;
    
    /* Устанавливаем page directory */
    paging_set_directory(current_page_dir);
    
    kputs("[PAGING] Initialized\n");
    return 0;
}

/* Установка page directory */
void paging_set_directory(uint32_t* page_dir)
{
    current_page_dir = page_dir;
    __asm__ volatile ("mov %0, %%cr3" :: "r"(page_dir) : "memory");
}

/* Получение текущего page directory */
uint32_t* paging_get_directory(void)
{
    return current_page_dir;
}

/* Отображение страницы */
int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    if (!current_page_dir)
        return -1;
    
    pte_t* pte = get_pte(current_page_dir, virt, 1);
    if (!pte)
        return -1;
    
    pte->present = (flags & PAGE_PRESENT) ? 1 : 0;
    pte->rw = (flags & PAGE_WRITE) ? 1 : 0;
    pte->user = (flags & PAGE_USER) ? 1 : 0;
    pte->frame = PAGE_FRAME(phys);
    
    /* Инвалидация TLB для этой страницы */
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    
    return 0;
}

/* Снятие отображения страницы */
int paging_unmap_page(uint32_t virt)
{
    if (!current_page_dir)
        return -1;
    
    pte_t* pte = get_pte(current_page_dir, virt, 0);
    if (!pte || !pte->present)
        return -1;
    
    pte->present = 0;
    pte->frame = 0;
    
    /* Инвалидация TLB */
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    
    return 0;
}

/* Отображение нескольких страниц */
int paging_map_pages(uint32_t virt, uint32_t phys, uint32_t count, uint32_t flags)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (paging_map_page(virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags) != 0)
            return -1;
    }
    return 0;
}

/* Получение физического адреса по виртуальному */
uint32_t paging_get_physical(uint32_t virt)
{
    if (!current_page_dir)
        return 0;
    
    pte_t* pte = get_pte(current_page_dir, virt, 0);
    if (!pte || !pte->present)
        return 0;
    
    return FRAME_ADDR(pte->frame) + PAGE_OFFSET(virt);
}

/* Включение paging */
void paging_enable(void)
{
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; /* Установка бита PG (Paging Enable) */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
    
    kputs("[PAGING] Enabled\n");
}

/* Отключение paging */
void paging_disable(void)
{
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x80000000; /* Сброс бита PG */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
}

/* Выделение страницы для page table */
uint32_t* paging_alloc_page_table(void)
{
    uint32_t frame = pmm_alloc_page();
    if (!frame)
        return NULL;
    
    uint32_t* table = (uint32_t*)frame;
    for (int i = 0; i < 1024; i++)
        table[i] = 0;
    
    return table;
}

/* Освобождение page table */
void paging_free_page_table(uint32_t* table)
{
    if (table)
        pmm_free_page((uint32_t)table);
}

