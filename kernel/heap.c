#include "../include/heap.h"
#include "../include/vmm.h"
#include "../include/console.h"
#include "../include/common.h"

/* Глобальный heap ядра */
struct heap kernel_heap;

/* Объединение свободных блоков */
static void heap_merge_free_blocks(struct heap* h)
{
    struct heap_block* current = h->first;
    
    while (current && current->next)
    {
        if (current->free && current->next->free)
        {
            /* Объединяем два соседних свободных блока */
            current->size += current->next->size;
            current->next = current->next->next;
            if (current->next)
                current->next->prev = current;
        }
        else
        {
            current = current->next;
        }
    }
}

/* Разделение блока, если он слишком большой */
static void heap_split_block(struct heap_block* block, uint32_t size)
{
    if (!block || block->size < size + sizeof(struct heap_block) + HEAP_MIN_BLOCK_SIZE)
        return;
    
    /* Создаем новый свободный блок после выделенного */
    struct heap_block* new_block = (struct heap_block*)((uint8_t*)block + sizeof(struct heap_block) + size);
    new_block->size = block->size - size - sizeof(struct heap_block);
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;
    
    if (block->next)
        block->next->prev = new_block;
    
    block->next = new_block;
    block->size = size + sizeof(struct heap_block);
}

/* Инициализация heap */
int heap_init(struct heap* h, void* start, uint32_t size)
{
    if (!h || !start || size < sizeof(struct heap_block) * 2)
        return -1;
    
    h->start = start;
    h->end = (uint8_t*)start + size;
    h->size = size;
    h->free_size = size - sizeof(struct heap_block);
    
    /* Создаем первый блок, занимающий весь heap */
    h->first = (struct heap_block*)start;
    h->first->size = size;
    h->first->free = 1;
    h->first->next = NULL;
    h->first->prev = NULL;
    
    return 0;
}

/* Выделение памяти */
void* heap_alloc(struct heap* h, uint32_t size)
{
    if (!h || size == 0)
        return NULL;
    
    /* Выравнивание размера */
    if (size < HEAP_MIN_BLOCK_SIZE)
        size = HEAP_MIN_BLOCK_SIZE;
    
    size = (size + 3) & ~3; /* Выравнивание по 4 байта */
    
    /* Поиск подходящего свободного блока */
    struct heap_block* current = h->first;
    
    while (current)
    {
        if (current->free && current->size >= size + sizeof(struct heap_block))
        {
            /* Найден подходящий блок */
            current->free = 0;
            
            /* Разделяем блок, если он слишком большой */
            heap_split_block(current, size);
            
            h->free_size -= current->size;
            
            /* Возвращаем адрес данных (после заголовка) */
            return (uint8_t*)current + sizeof(struct heap_block);
        }
        
        current = current->next;
    }
    
    return NULL; /* Недостаточно памяти */
}

/* Освобождение памяти */
void heap_free(struct heap* h, void* ptr)
{
    if (!h || !ptr)
        return;
    
    /* Получаем блок из указателя */
    struct heap_block* block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    /* Проверка валидности блока */
    if (block < h->first || (uint8_t*)block > (uint8_t*)h->end)
        return;
    
    if (block->free)
        return; /* Уже свободен */
    
    /* Освобождаем блок */
    block->free = 1;
    h->free_size += block->size;
    
    /* Объединяем соседние свободные блоки */
    heap_merge_free_blocks(h);
}

/* Изменение размера блока */
void* heap_realloc(struct heap* h, void* ptr, uint32_t size)
{
    if (!ptr)
        return heap_alloc(h, size);
    
    if (size == 0)
    {
        heap_free(h, ptr);
        return NULL;
    }
    
    /* Получаем текущий блок */
    struct heap_block* block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    /* Выравнивание размера */
    if (size < HEAP_MIN_BLOCK_SIZE)
        size = HEAP_MIN_BLOCK_SIZE;
    size = (size + 3) & ~3;
    
    uint32_t old_size = block->size - sizeof(struct heap_block);
    
    /* Если новый размер меньше или равен текущему */
    if (size <= old_size)
    {
        heap_split_block(block, size);
        return ptr;
    }
    
    /* Если следующий блок свободен и достаточно большой */
    if (block->next && block->next->free)
    {
        uint32_t combined_size = block->size + block->next->size - sizeof(struct heap_block);
        if (combined_size >= size + sizeof(struct heap_block))
        {
            /* Объединяем с следующим блоком */
            block->size = combined_size;
            block->next = block->next->next;
            if (block->next)
                block->next->prev = block;
            
            heap_split_block(block, size);
            return ptr;
        }
    }
    
    /* Нужно выделить новый блок и скопировать данные */
    void* new_ptr = heap_alloc(h, size);
    if (new_ptr)
    {
        /* Копируем данные */
        uint32_t copy_size = (old_size < size) ? old_size : size;
        for (uint32_t i = 0; i < copy_size; i++)
        {
            ((uint8_t*)new_ptr)[i] = ((uint8_t*)ptr)[i];
        }
        
        heap_free(h, ptr);
    }
    
    return new_ptr;
}

/* Получение информации о heap */
uint32_t heap_get_free_size(struct heap* h)
{
    return h ? h->free_size : 0;
}

uint32_t heap_get_used_size(struct heap* h)
{
    return h ? (h->size - h->free_size) : 0;
}

uint32_t heap_get_total_size(struct heap* h)
{
    return h ? h->size : 0;
}

/* Инициализация глобального heap ядра */
static int kernel_heap_initialized = 0;

void* kmalloc(uint32_t size)
{
    if (!kernel_heap_initialized)
        return NULL;
    
    return heap_alloc(&kernel_heap, size);
}

void kfree(void* ptr)
{
    if (!kernel_heap_initialized)
        return;
    
    heap_free(&kernel_heap, ptr);
}

void* krealloc(void* ptr, uint32_t size)
{
    if (!kernel_heap_initialized)
        return NULL;
    
    return heap_realloc(&kernel_heap, ptr, size);
}

/* Функция инициализации kernel heap (вызывается из vmm_init или отдельно) */
int kernel_heap_init(void)
{
    uint32_t heap_size = KERNEL_HEAP_END - KERNEL_HEAP_START;
    
    /* Выделяем виртуальные страницы для heap */
    uint32_t pages = (heap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    void* virt = vmm_alloc_pages(pages, PAGE_KERNEL);
    
    if (!virt)
    {
        kputs("[HEAP] Error: Failed to allocate heap pages\n");
        return -1;
    }
    
    if (heap_init(&kernel_heap, virt, heap_size) != 0)
    {
        kputs("[HEAP] Error: Failed to initialize heap\n");
        return -1;
    }
    
    kernel_heap_initialized = 1;
    
    kputs("[HEAP] Kernel heap initialized: ");
    kprint_dec(heap_size / 1024);
    kputs(" KB\n");
    
    return 0;
}

