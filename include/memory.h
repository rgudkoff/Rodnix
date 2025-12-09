#ifndef _RODNIX_MEMORY_H
#define _RODNIX_MEMORY_H

#include "types.h"

/* Флаги для защиты памяти */
#define PAGE_PRESENT  0x001  /* Страница присутствует в памяти */
#define PAGE_WRITE    0x002  /* Страница доступна для записи */
#define PAGE_USER     0x004  /* Страница доступна пользователю */
#define PAGE_PWT      0x008  /* Page Write Through */
#define PAGE_PCD      0x010  /* Page Cache Disable */
#define PAGE_ACCESSED 0x020  /* Страница была прочитана */
#define PAGE_DIRTY    0x040  /* Страница была изменена */
#define PAGE_SIZE_4M  0x080  /* Размер страницы 4MB (для PDE) */
#define PAGE_GLOBAL   0x100  /* Глобальная страница (TLB) */
#define PAGE_NX       0x80000000  /* No Execute (NX bit) - для защиты от выполнения */

/* Комбинированные флаги */
#define PAGE_KERNEL (PAGE_PRESENT | PAGE_WRITE)
#define PAGE_KERNEL_RO (PAGE_PRESENT)
#define PAGE_USER_RO (PAGE_PRESENT | PAGE_USER)
#define PAGE_USER_RW (PAGE_PRESENT | PAGE_WRITE | PAGE_USER)
#define PAGE_USER_RX (PAGE_PRESENT | PAGE_USER)  /* Read + Execute (без NX) */
#define PAGE_USER_RWX (PAGE_PRESENT | PAGE_WRITE | PAGE_USER)  /* Read + Write + Execute (без NX) */

/* W^X: Write XOR Execute - страница не может быть одновременно записываемой и исполняемой */
#define PAGE_WX_MASK (PAGE_WRITE | PAGE_NX)
#define PAGE_WRITE_ONLY (PAGE_PRESENT | PAGE_WRITE | PAGE_NX)
#define PAGE_EXECUTE_ONLY (PAGE_PRESENT | PAGE_NX)  /* NX=0 означает executable */

/* Копирование данных между ядром и пользовательским пространством */
int copy_to_user(void* user_dst, const void* kernel_src, uint32_t size);
int copy_from_user(void* kernel_dst, const void* user_src, uint32_t size);

/* Проверка валидности пользовательского указателя */
int is_user_address_valid(const void* addr, uint32_t size);

/* Настройка NX бита в CR4 (если поддерживается CPU) */
void enable_nx_bit(void);

/* Проверка поддержки NX бита */
int is_nx_supported(void);

#endif

