#include "../include/memory.h"
#include "../include/paging.h"
#include "../include/console.h"

/* Проверка валидности пользовательского указателя */
int is_user_address_valid(const void* addr, uint32_t size)
{
    uint32_t addr_val = (uint32_t)addr;
    uint32_t end_addr = addr_val + size;
    
    /* Проверка на переполнение */
    if (end_addr < addr_val)
        return 0;
    
    /* Пользовательское пространство: 0x00000000 - 0xBFFFFFFF (3GB) */
    /* Ядро: 0xC0000000 - 0xFFFFFFFF (1GB) */
    if (addr_val >= 0xC0000000)
        return 0; /* Адрес в пространстве ядра */
    
    if (end_addr > 0xC0000000)
        return 0; /* Пересекается с пространством ядра */
    
    return 1; /* Валидный пользовательский адрес */
}

/* Копирование данных из ядра в пользовательское пространство */
int copy_to_user(void* user_dst, const void* kernel_src, uint32_t size)
{
    if (!is_user_address_valid(user_dst, size))
        return -1;
    
    if (!kernel_src || size == 0)
        return -1;
    
    /* Проверка, что kernel_src находится в пространстве ядра */
    uint32_t kernel_addr = (uint32_t)kernel_src;
    if (kernel_addr < 0xC0000000)
        return -1; /* Не является адресом ядра */
    
    /* Копирование с проверкой страниц */
    uint8_t* dst = (uint8_t*)user_dst;
    const uint8_t* src = (const uint8_t*)kernel_src;
    
    for (uint32_t i = 0; i < size; i++)
    {
        /* Проверка, что страница пользователя доступна для записи */
        uint32_t page_addr = ((uint32_t)dst + i) & ~0xFFF;
        uint32_t phys = paging_get_physical(page_addr);
        
        if (phys == 0)
            return -1; /* Страница не отображена */
        
        /* Копирование байта */
        dst[i] = src[i];
    }
    
    return 0;
}

/* Копирование данных из пользовательского пространства в ядро */
int copy_from_user(void* kernel_dst, const void* user_src, uint32_t size)
{
    if (!is_user_address_valid(user_src, size))
        return -1;
    
    if (!kernel_dst || size == 0)
        return -1;
    
    /* Проверка, что kernel_dst находится в пространстве ядра */
    uint32_t kernel_addr = (uint32_t)kernel_dst;
    if (kernel_addr < 0xC0000000)
        return -1; /* Не является адресом ядра */
    
    /* Копирование с проверкой страниц */
    uint8_t* dst = (uint8_t*)kernel_dst;
    const uint8_t* src = (const uint8_t*)user_src;
    
    for (uint32_t i = 0; i < size; i++)
    {
        /* Проверка, что страница пользователя доступна для чтения */
        uint32_t page_addr = ((uint32_t)src + i) & ~0xFFF;
        uint32_t phys = paging_get_physical(page_addr);
        
        if (phys == 0)
            return -1; /* Страница не отображена */
        
        /* Копирование байта */
        dst[i] = src[i];
    }
    
    return 0;
}

/* Проверка поддержки NX бита (No Execute) */
int is_nx_supported(void)
{
    /* Для упрощения, предполагаем что NX поддерживается на современных CPU */
    /* В реальной реализации нужно проверять через CPUID */
    /* TODO: Implement proper CPUID check */
    return 1; /* Assume supported for now */
}

/* Включение NX бита в CR4 (если поддерживается) */
/* ВАЖНО: В 32-bit режиме NX требует PAE, но мы используем стандартный пейджинг без PAE */
/* Поэтому NX отключен для совместимости со стандартным пейджингом */
void enable_nx_bit(void)
{
    /* NX в 32-bit режиме требует PAE, но мы используем стандартный пейджинг */
    /* PAE требует другую структуру таблиц страниц (PDPT), что несовместимо */
    /* Поэтому NX отключен - можно включить позже при переходе на PAE или 64-bit */
    kputs("[MEMORY] NX bit disabled (requires PAE, but we use standard 32-bit paging)\n");
    return;
    
    /* Старый код (закомментирован) - требует PAE */
    /*
    if (!is_nx_supported())
    {
        kputs("[MEMORY] NX bit not supported by CPU\n");
        return;
    }
    
    uint32_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    
    // Установка PAE для NX - НЕ СОВМЕСТИМО со стандартным пейджингом!
    // cr4 |= (1 << 5);  // PAE (Physical Address Extension)
    
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");
    kputs("[MEMORY] NX bit enabled\n");
    */
}

