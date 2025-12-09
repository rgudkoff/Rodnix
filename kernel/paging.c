#include "../include/paging.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/console.h"
#include "../include/common.h"
#include "../include/idt.h"
#include "../include/gdt.h"

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
        {
            /* Не выводим ошибку здесь, так как это может быть слишком много вывода */
            return NULL;
        }
        
        /* Очищаем page table - преобразуем физический адрес в виртуальный */
        /* Если адрес в первых 4MB, используем identity mapping, иначе PHYS_TO_VIRT */
        uint32_t* table;
        if (table_frame < 0x400000)
        {
            table = (uint32_t*)table_frame;  /* Identity mapped */
        }
        else
        {
            table = (uint32_t*)PHYS_TO_VIRT(table_frame);  /* Kernel virtual space */
        }
        for (int i = 0; i < 1024; i++)
            table[i] = 0;
        
        /* Устанавливаем PDE */
        pde->present = 1;
        pde->rw = 1;
        pde->user = 0;
        pde->frame = PAGE_FRAME(table_frame);
    }
    
    /* Получаем адрес page table - преобразуем физический адрес в виртуальный */
    uint32_t phys_table = FRAME_ADDR(pde->frame);
    uint32_t* page_table;
    if (phys_table < 0x400000)
    {
        page_table = (uint32_t*)phys_table;  /* Identity mapped */
    }
    else
    {
        page_table = (uint32_t*)PHYS_TO_VIRT(phys_table);  /* Kernel virtual space */
    }
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
    
    /* Page directory должен быть в identity mapped области или преобразован в виртуальный */
    if (dir_frame < 0x400000)
    {
        current_page_dir = (uint32_t*)dir_frame;  /* Identity mapped */
    }
    else
    {
        current_page_dir = (uint32_t*)PHYS_TO_VIRT(dir_frame);  /* Kernel virtual space */
    }
    
    /* Очищаем page directory */
    for (int i = 0; i < 1024; i++)
        current_page_dir[i] = 0;
    
    /* Устанавливаем page directory */
    paging_set_directory(current_page_dir);
    
    return 0;
}

/* Установка page directory */
void paging_set_directory(uint32_t* page_dir)
{
    current_page_dir = page_dir;
    /* CR3 требует физический адрес, преобразуем виртуальный в физический если нужно */
    uint32_t phys_dir;
    uint32_t virt_dir = (uint32_t)page_dir;
    if (virt_dir >= 0xC0000000)
    {
        phys_dir = VIRT_TO_PHYS(virt_dir);
    }
    else
    {
        phys_dir = virt_dir;  /* Уже физический адрес (identity mapped) */
    }
    
    __asm__ volatile ("mov %0, %%cr3" :: "r"(phys_dir) : "memory");
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
    /* Проверяем, что page directory установлен */
    if (!current_page_dir)
    {
        kputs("[PAGING] ERROR: Page directory not set!\n");
        return;
    }
    
    /* Убеждаемся, что CR3 установлен перед включением пейджинга */
    /* CR3 требует физический адрес page directory */
    uint32_t phys_dir;
    uint32_t virt_dir = (uint32_t)current_page_dir;
    if (virt_dir >= 0xC0000000)
    {
        phys_dir = VIRT_TO_PHYS(virt_dir);
    }
    else
    {
        phys_dir = virt_dir;  /* Уже физический адрес (identity mapped) */
    }
    
    /* Устанавливаем CR3 */
    __asm__ volatile (
        "mov %0, %%eax\n\t"
        "mov %%eax, %%cr3"
        :: "r"(phys_dir) : "eax", "memory");
    
    /* Убеждаемся, что все page tables отображены */
    uint32_t* test_pd = current_page_dir;
    for (uint32_t i = 0; i < 1024; i++)
    {
        pde_t* pde = (pde_t*)&test_pd[i];
        if (pde->present)
        {
            uint32_t table_phys = FRAME_ADDR(pde->frame);
            if (table_phys < 0x400000)
            {
                uint32_t table_phys_mapped = paging_get_physical(table_phys);
                if (table_phys_mapped != table_phys)
                {
                    paging_map_page(table_phys, table_phys, PAGE_KERNEL);
                }
            }
        }
    }
    
    /* Получаем текущий CR0 и устанавливаем бит PG */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    
    /* Критические проверки перед включением пейджинга */
    /* 1. Проверка выравнивания CR3 */
    if ((phys_dir & 0xFFF) != 0)
    {
        kputs("[PAGING] ERROR: CR3 not page-aligned!\n");
        return;
    }
    
    /* 2. Проверка флагов первой PDE */
    uint32_t* test_pd_sanity = current_page_dir;
    pde_t* first_pde_sanity = (pde_t*)&test_pd_sanity[0];
    if (!first_pde_sanity->present || !first_pde_sanity->rw)
    {
        kputs("[PAGING] ERROR: First PDE flags incorrect!\n");
        return;
    }
    
    /* 3. Проверка identity mapping для кода и стека */
    uint32_t current_eip;
    __asm__ volatile ("call 1f\n\t" "1: pop %0" : "=r"(current_eip));
    uint32_t current_esp;
    __asm__ volatile ("mov %%esp, %0" : "=r"(current_esp));
    
    uint32_t eip_phys = paging_get_physical(current_eip);
    uint32_t esp_phys = paging_get_physical(current_esp);
    
    if (eip_phys != current_eip || esp_phys != current_esp)
    {
        kputs("[PAGING] ERROR: Code or stack not identity mapped!\n");
        return;
    }
    
    /* 4. Проверка и очистка CR4 (должен быть 0 для стандартного пейджинга) */
    uint32_t cr4_before;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4_before));
    if (cr4_before != 0)
    {
        kputs("[PAGING] Clearing CR4 (was 0x");
        kprint_hex(cr4_before);
        kputs(")...\n");
        uint32_t cr4_zero = 0;
        __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4_zero) : "memory");
    }
    
    kputs("[PAGING] Enabling paging...\n");
    
    /* КРИТИЧНО: После включения пейджинга следующая инструкция выполняется уже в paged режиме */
    /* Если происходит page fault, обработчик должен быть доступен */
    /* Используем минимальный подход: только mov cr0 + запись в VGA через регистры */
    /* Это позволит проверить, доходит ли выполнение до этой точки */
    /* Если на экране появится 'OK', значит пейджинг включен и код работает */
    uint32_t cr0_after_enable = 0;
    __asm__ volatile (
        "mov %1, %%cr0\n\t"            /* Включаем пейджинг - КРИТИЧЕСКАЯ ТОЧКА */
        "mov %%cr0, %0\n\t"            /* Читаем CR0 обратно - если дошли сюда, пейджинг включен */
        "mov $0xB8000, %%edi\n\t"      /* Адрес VGA в EDI */
        "mov $0x4F4F, %%ax\n\t"        /* 'OO' белый на красном - признак успеха */
        "mov %%ax, (%%edi)\n\t"        /* Записываем 'OO' в VGA */
        "mov $0x4B4B, %%ax\n\t"        /* 'KK' белый на красном */
        "mov %%ax, 2(%%edi)\n\t"       /* Записываем 'KK' в следующую позицию */
        "mov $0x4F21, %%ax\n\t"        /* '! ' белый на красном */
        "mov %%ax, 4(%%edi)"           /* Записываем '! ' - полный признак 'OOKK!' */
        : "=r"(cr0_after_enable)        /* Вывод: CR0 после включения */
        : "r"(cr0)                      /* Ввод: значение CR0 */
        : "edi", "ax", "memory");
    
    /* Если мы дошли сюда, значит следующая инструкция выполнилась после включения пейджинга */
    /* Это означает, что пейджинг работает! */
    kputs("[PAGING] Paging enabled successfully!\n");
    kputs("  [PAGING] CR0 after enable: ");
    kprint_hex(cr0_after_enable);
    kputs((cr0_after_enable & 0x80000000) ? " [PG bit is set - OK]\n" : " [PG bit NOT set - ERROR]\n");
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
    {
        uint32_t phys = (uint32_t)table;
        if (phys >= 0xC0000000)
            phys = VIRT_TO_PHYS(phys);
        pmm_free_page(phys);
    }
}

