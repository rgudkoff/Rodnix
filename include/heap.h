#ifndef _RODNIX_HEAP_H
#define _RODNIX_HEAP_H

#include "types.h"
#include "vmm.h"

/* Размеры блоков для heap */
#define HEAP_MIN_BLOCK_SIZE 16
#define HEAP_MAX_BLOCK_SIZE (PAGE_SIZE * 4)  /* 16KB максимум */

/* Структура блока heap */
struct heap_block {
    uint32_t size;              /* Размер блока (включая заголовок) */
    uint8_t free;               /* 1 = свободен, 0 = занят */
    struct heap_block* next;    /* Следующий блок */
    struct heap_block* prev;   /* Предыдущий блок */
} __attribute__((packed));

/* Структура heap */
struct heap {
    void* start;                /* Начало heap */
    void* end;                  /* Конец heap */
    uint32_t size;              /* Размер heap */
    uint32_t free_size;         /* Свободная память */
    struct heap_block* first;   /* Первый блок */
};

/* Инициализация heap */
int heap_init(struct heap* h, void* start, uint32_t size);

/* Выделение памяти */
void* heap_alloc(struct heap* h, uint32_t size);
void heap_free(struct heap* h, void* ptr);

/* Изменение размера блока */
void* heap_realloc(struct heap* h, void* ptr, uint32_t size);

/* Получение информации о heap */
uint32_t heap_get_free_size(struct heap* h);
uint32_t heap_get_used_size(struct heap* h);
uint32_t heap_get_total_size(struct heap* h);

/* Глобальный heap ядра */
extern struct heap kernel_heap;

/* Функции для работы с глобальным heap ядра */
int kernel_heap_init(void);
void* kmalloc(uint32_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, uint32_t size);

#endif

