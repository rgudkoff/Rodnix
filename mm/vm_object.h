/**
 * @file vm_object.h
 * @brief Объект памяти: то, что страницы хранит, а не то, что их отображает.
 *
 * Резидентные страницы — разрежённый индекс, а не плотный массив. Массив
 * платил память за потенциальный размер объекта (гигабайтный mmap — два
 * мегабайта таблицы при нуле резидентных страниц) и не имел замка вовсе.
 * Индекс платит за фактическую резидентность: корзины хеша — массив индексов
 * vm_page, цепочки продеты сквозь сами страницы (vm_page.obj_next), так что
 * вставка не выделяет памяти никогда — узел индекса и страница суть одно,
 * как лист radix-дерева у FreeBSD.
 *
 * Замок — свой, на объект. Пока весь VM ходит под глобальным vm_lock, он
 * дублирует защиту; нагрузку он примет на этапах 5–6, когда vm_lock умрёт.
 * Ввод-вывод под ним не делается: пейджер читает страницу до того, как она
 * вставляется в индекс.
 */

#ifndef _RODNIX_VM_OBJECT_H
#define _RODNIX_VM_OBJECT_H

#include <stdint.h>
#include "../kernel/fabric/spin.h"

#define VM_OBJECT_PAGE_SIZE 0x1000ULL

typedef enum {
    VM_OBJECT_ANON = 1,
    VM_OBJECT_FILE = 2
} vm_object_type_t;

/*
 * Класс обслуживания — вторая ось mm_redesign.md: цена ошибки при возврате.
 * Носитель класса — объект, потому что у одного приложения одновременно
 * есть память, которую трогать нельзя (кольцевой буфер аудио), и память,
 * которую можно выбросить (кэш превью). Полоса на процесс этого не различает.
 * Процесс задаёт умолчание, RT-поток повышает класс объектов, которых
 * касается, — политика этапа 7; поле живёт здесь с этапа 4, чтобы каждый
 * объект с рождения знал, чем он оплачен.
 */
typedef enum {
    VM_CLASS_RT = 0,     /* закреплено; возврат запрещён при любом давлении */
    VM_CLASS_PROJECT,    /* открытые проекты: выгружать — последним делом */
    VM_CLASS_NORMAL,     /* обычная память */
    VM_CLASS_CACHE,      /* кэш: чистое выбрасывается первым */
    VM_CLASS_VOLATILE,   /* воспроизводимое: выбросить, а не выгрузить */
} vm_class_t;

struct vm_page;

typedef struct vm_object {
    vm_object_type_t type;
    uint8_t vm_class;          /* vm_class_t; VM_CLASS_NORMAL по умолчанию */
    uint32_t ref_count;        /* атомарный; последний unref разрушает объект */
    uint64_t size;             /* байты, как объект создали */
    uint64_t page_count;       /* верхняя граница pindex (страниц в size) */

    /* Разрежённый индекс резидентных страниц. Корзина — индекс первой
     * vm_page цепочки (VM_PAGE_NIL — пусто), цепочка — vm_page.obj_next.
     * nbuckets — степень двойки; ноль, пока не вставлена первая страница.
     * Всё под lock. */
    spinlock_t lock;
    uint32_t* buckets;
    uint32_t nbuckets;
    uint32_t resident;

    void* pager_private;
} vm_object_t;

typedef struct vm_file_backing {
    const uint8_t* data;
    uint64_t size;
    uint64_t file_offset;
    /* Optional demand-paging callback: fills one VM_OBJECT_PAGE_SIZE page.
     * page_off is the byte offset from the start of the file (page-aligned).
     * Returns RDNX_OK on success. */
    int (*read_page)(void* pager_priv, uint64_t page_off, void* page_buf);
    void* pager_priv;
} vm_file_backing_t;

vm_object_t* vm_object_create(vm_object_type_t type, uint64_t size);
void vm_object_ref(vm_object_t* obj);
void vm_object_unref(vm_object_t* obj);
uint64_t vm_object_get_resident_page(vm_object_t* obj, uint64_t page_index);
int vm_object_set_resident_page(vm_object_t* obj, uint64_t page_index, uint64_t phys);
uint32_t vm_object_resident_count(vm_object_t* obj);

#endif /* _RODNIX_VM_OBJECT_H */
