#include "../include/pmm.h"
#include "../include/console.h"
#include "../include/common.h"

/* Bitmap для отслеживания страниц */
static uint8_t* page_bitmap = NULL;
static uint32_t bitmap_size = 0;
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;
static uint32_t mem_start = 0;
static uint32_t mem_end = 0;

/* Макросы для работы с bitmap */
#define BITMAP_SET(bit) (page_bitmap[(bit) / 8] |= (1 << ((bit) % 8)))
#define BITMAP_CLEAR(bit) (page_bitmap[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define BITMAP_TEST(bit) (page_bitmap[(bit) / 8] & (1 << ((bit) % 8)))

/* Инициализация PMM */
int pmm_init(uint32_t mem_start_addr, uint32_t mem_end_addr)
{
    mem_start = PAGE_ALIGN(mem_start_addr);
    mem_end = PAGE_ALIGN_DOWN(mem_end_addr);
    
    if (mem_end <= mem_start)
    {
        kputs("[PMM] Error: Invalid memory range\n");
        return -1;
    }
    
    /* Вычисление количества страниц */
    total_pages = (mem_end - mem_start) / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8; /* Округление вверх до байта */
    
    /* Bitmap должен быть размещен в начале доступной памяти */
    /* Используем область сразу после ядра (примерно 2MB) */
    page_bitmap = (uint8_t*)0x200000; /* 2MB - место для bitmap */
    
    /* Очистка bitmap */
    for (uint32_t i = 0; i < bitmap_size; i++)
        page_bitmap[i] = 0;
    
    /* Резервирование области для bitmap и ядра */
    /* Резервируем первые 3MB (ядро + bitmap) */
    uint32_t reserved_start_frame = 0;
    uint32_t reserved_end_frame = PAGE_FRAME((uintptr_t)page_bitmap + bitmap_size);
    
    for (uint32_t i = reserved_start_frame; i <= reserved_end_frame; i++)
    {
        if (i < total_pages)
            BITMAP_SET(i);
    }
    
    used_pages = reserved_end_frame - reserved_start_frame + 1;
    
    kputs("[PMM] Initialized: ");
    kprint_dec(total_pages);
    kputs(" pages (");
    kprint_dec((total_pages * PAGE_SIZE) / 1024);
    kputs(" KB total)\n");
    
    return 0;
}

/* Выделение физической страницы */
uint32_t pmm_alloc_page(void)
{
    if (!page_bitmap)
        return 0;
    
    /* Поиск свободной страницы */
    for (uint32_t i = 0; i < total_pages; i++)
    {
        if (!BITMAP_TEST(i))
        {
            BITMAP_SET(i);
            used_pages++;
            return FRAME_ADDR(i);
        }
    }
    
    return 0; /* Нет свободных страниц */
}

/* Освобождение физической страницы */
void pmm_free_page(uint32_t frame)
{
    if (!page_bitmap || frame < mem_start)
        return;
    
    uint32_t page = PAGE_FRAME(frame);
    if (page >= total_pages)
        return;
    
    if (BITMAP_TEST(page))
    {
        BITMAP_CLEAR(page);
        if (used_pages > 0)
            used_pages--;
    }
}

/* Выделение нескольких страниц */
uint32_t pmm_alloc_pages(uint32_t count)
{
    if (!page_bitmap || count == 0)
        return 0;
    
    /* Поиск последовательности свободных страниц */
    uint32_t consecutive = 0;
    uint32_t start_page = 0;
    
    for (uint32_t i = 0; i < total_pages; i++)
    {
        if (!BITMAP_TEST(i))
        {
            if (consecutive == 0)
                start_page = i;
            consecutive++;
            
            if (consecutive >= count)
            {
                /* Помечаем страницы как занятые */
                for (uint32_t j = start_page; j < start_page + count; j++)
                {
                    BITMAP_SET(j);
                }
                used_pages += count;
                return FRAME_ADDR(start_page);
            }
        }
        else
        {
            consecutive = 0;
        }
    }
    
    return 0; /* Недостаточно последовательных страниц */
}

/* Освобождение нескольких страниц */
void pmm_free_pages(uint32_t frame, uint32_t count)
{
    if (!page_bitmap || count == 0)
        return;
    
    uint32_t start_page = PAGE_FRAME(frame);
    
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t page = start_page + i;
        if (page < total_pages && BITMAP_TEST(page))
        {
            BITMAP_CLEAR(page);
            if (used_pages > 0)
                used_pages--;
        }
    }
}

/* Получение информации о памяти */
uint32_t pmm_get_total_pages(void)
{
    return total_pages;
}

uint32_t pmm_get_free_pages(void)
{
    return total_pages - used_pages;
}

uint32_t pmm_get_used_pages(void)
{
    return used_pages;
}

/* Резервирование области памяти */
void pmm_reserve_region(uint32_t start, uint32_t end)
{
    if (!page_bitmap)
        return;
    
    uint32_t start_page = PAGE_FRAME(PAGE_ALIGN(start));
    uint32_t end_page = PAGE_FRAME(PAGE_ALIGN_DOWN(end));
    
    for (uint32_t i = start_page; i <= end_page && i < total_pages; i++)
    {
        if (!BITMAP_TEST(i))
        {
            BITMAP_SET(i);
            used_pages++;
        }
    }
}

/* Снятие резервирования области памяти */
void pmm_unreserve_region(uint32_t start, uint32_t end)
{
    if (!page_bitmap)
        return;
    
    uint32_t start_page = PAGE_FRAME(PAGE_ALIGN(start));
    uint32_t end_page = PAGE_FRAME(PAGE_ALIGN_DOWN(end));
    
    for (uint32_t i = start_page; i <= end_page && i < total_pages; i++)
    {
        if (BITMAP_TEST(i))
        {
            BITMAP_CLEAR(i);
            if (used_pages > 0)
                used_pages--;
        }
    }
}

