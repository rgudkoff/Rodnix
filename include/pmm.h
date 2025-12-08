#ifndef _RODNIX_PMM_H
#define _RODNIX_PMM_H

#include "types.h"

/* Размер страницы (4KB для i386) */
#define PAGE_SIZE 4096
#define PAGE_SIZE_BITS 12

/* Макросы для работы со страницами */
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))
#define PAGE_FRAME(addr) ((addr) >> PAGE_SIZE_BITS)
#define FRAME_ADDR(frame) ((frame) << PAGE_SIZE_BITS)

/* Статусы страницы */
#define PAGE_FREE 0
#define PAGE_USED 1
#define PAGE_RESERVED 2

/* Инициализация PMM */
int pmm_init(uint32_t mem_start, uint32_t mem_end);

/* Выделение физической страницы */
uint32_t pmm_alloc_page(void);
void pmm_free_page(uint32_t frame);

/* Выделение нескольких страниц */
uint32_t pmm_alloc_pages(uint32_t count);
void pmm_free_pages(uint32_t frame, uint32_t count);

/* Получение информации о памяти */
uint32_t pmm_get_total_pages(void);
uint32_t pmm_get_free_pages(void);
uint32_t pmm_get_used_pages(void);

/* Резервирование области памяти */
void pmm_reserve_region(uint32_t start, uint32_t end);
void pmm_unreserve_region(uint32_t start, uint32_t end);

#endif

