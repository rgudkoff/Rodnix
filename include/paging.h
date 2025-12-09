#ifndef _RODNIX_PAGING_H
#define _RODNIX_PAGING_H

#include "types.h"
#include "pmm.h"

/* Флаги страницы */
#define PAGE_PRESENT  0x001  /* Страница присутствует в памяти */
#define PAGE_WRITE    0x002  /* Страница доступна для записи */
#define PAGE_USER     0x004  /* Страница доступна пользователю */
#define PAGE_PWT      0x008  /* Page Write Through */
#define PAGE_PCD      0x010  /* Page Cache Disable */
#define PAGE_ACCESSED 0x020  /* Страница была прочитана */
#define PAGE_DIRTY    0x040  /* Страница была изменена */
#define PAGE_SIZE_4M  0x080  /* Размер страницы 4MB (для PDE) */
#define PAGE_GLOBAL   0x100  /* Глобальная страница (TLB) */

/* Комбинированные флаги */
#define PAGE_KERNEL (PAGE_PRESENT | PAGE_WRITE)
#define PAGE_USER_RO (PAGE_PRESENT | PAGE_USER)
#define PAGE_USER_RW (PAGE_PRESENT | PAGE_WRITE | PAGE_USER)

/* Структура записи в Page Table (PTE) */
typedef struct {
    uint32_t present    : 1;  /* Страница присутствует */
    uint32_t rw         : 1;  /* Read/Write */
    uint32_t user       : 1;  /* User/Supervisor */
    uint32_t pwt        : 1;  /* Page Write Through */
    uint32_t pcd        : 1;  /* Page Cache Disable */
    uint32_t accessed   : 1;  /* Accessed */
    uint32_t dirty      : 1;  /* Dirty */
    uint32_t pat        : 1;  /* Page Attribute Table */
    uint32_t global     : 1;  /* Global */
    uint32_t available  : 3;  /* Доступно для ОС */
    uint32_t frame      : 20; /* Физический адрес страницы */
} pte_t;

/* Структура записи в Page Directory (PDE) */
typedef struct {
    uint32_t present    : 1;
    uint32_t rw        : 1;
    uint32_t user      : 1;
    uint32_t pwt       : 1;
    uint32_t pcd       : 1;
    uint32_t accessed  : 1;
    uint32_t dirty     : 1;
    uint32_t size      : 1;  /* 0 = 4KB, 1 = 4MB */
    uint32_t global    : 1;
    uint32_t available : 3;
    uint32_t frame     : 20; /* Физический адрес page table или 4MB страницы */
} pde_t;

/* Макросы для работы с адресами */
#define PAGE_DIR_INDEX(addr) (((addr) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(addr) (((addr) >> 12) & 0x3FF)
#define PAGE_OFFSET(addr) ((addr) & 0xFFF)

/* Инициализация paging */
int paging_init(void);

/* Установка/получение page directory */
void paging_set_directory(uint32_t* page_dir);
uint32_t* paging_get_directory(void);

/* Управление страницами */
int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
int paging_unmap_page(uint32_t virt);
int paging_map_pages(uint32_t virt, uint32_t phys, uint32_t count, uint32_t flags);

/* Получение физического адреса по виртуальному */
uint32_t paging_get_physical(uint32_t virt);

/* Включение paging */
void paging_enable(void);

/* Отключение paging */
void paging_disable(void);

/* Выделение страницы для page table */
uint32_t* paging_alloc_page_table(void);
void paging_free_page_table(uint32_t* table);

/* Отладка paging */
void paging_debug_init(void);

#endif
