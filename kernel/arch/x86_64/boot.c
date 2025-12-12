/**
 * @file x86_64/boot.c
 * @brief Реализация загрузки для x86_64
 */

#include "../../core/boot.h"
#include "types.h"
#include "config.h"
#include <stddef.h>

static boot_info_t boot_info_storage;
static bool boot_info_valid = false;

int boot_early_init(boot_info_t* info)
{
    if (!info) {
        return -1;
    }
    
    boot_info_storage = *info;
    boot_info_valid = true;
    
    return 0;
}

int boot_arch_init(void)
{
    /* Инициализация архитектурно-зависимых компонентов x86_64 */
    /* GDT, IDT и т.д. уже инициализированы в boot.S */
    
    return 0;
}

int boot_switch_to_64bit(void)
{
    /* Переключение в 64-битный режим уже выполнено в boot.S */
    /* Эта функция вызывается для совместимости, но ничего не делает */
    
    return 0;
}

int boot_memory_init(boot_info_t* info)
{
    if (!info) {
        if (!boot_info_valid) {
            return -1;
        }
        info = &boot_info_storage;
    }
    
    /* Инициализация памяти на раннем этапе */
    /* PMM уже инициализирован */
    
    return 0;
}

int boot_interrupts_init(void)
{
    /* Инициализация прерываний на раннем этапе */
    /* IDT уже инициализирована */
    
    return 0;
}

boot_info_t* boot_get_info(void)
{
    if (!boot_info_valid) {
        return NULL;
    }
    
    return &boot_info_storage;
}

