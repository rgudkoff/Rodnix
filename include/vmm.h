#ifndef _RODNIX_VMM_H
#define _RODNIX_VMM_H

#include "types.h"
#include "paging.h"

/* Виртуальные адреса ядра */
#define KERNEL_VIRT_BASE 0xC0000000  /* 3GB - начало виртуального адресного пространства ядра */
#define KERNEL_HEAP_START 0xC0400000 /* Начало heap ядра */
#define KERNEL_HEAP_END   0xC0800000 /* Конец heap ядра (4MB) */

/* Макросы для преобразования адресов */
#define VIRT_TO_PHYS(addr) ((addr) - KERNEL_VIRT_BASE)
#define PHYS_TO_VIRT(addr) ((addr) + KERNEL_VIRT_BASE)

/* Инициализация VMM */
int vmm_init(void);

/* Выделение виртуальной памяти */
void* vmm_alloc(uint32_t size, uint32_t flags);
void vmm_free(void* virt, uint32_t size);

/* Выделение страницы */
void* vmm_alloc_page(uint32_t flags);
void vmm_free_page(void* virt);

/* Выделение нескольких страниц */
void* vmm_alloc_pages(uint32_t count, uint32_t flags);
void vmm_free_pages(void* virt, uint32_t count);

/* Отображение физической памяти в виртуальную */
void* vmm_map_physical(uint32_t phys, uint32_t size, uint32_t flags);
void vmm_unmap_physical(void* virt, uint32_t size);

/* Получение информации */
uint32_t vmm_get_total_memory(void);
uint32_t vmm_get_free_memory(void);
uint32_t vmm_get_used_memory(void);

#endif

