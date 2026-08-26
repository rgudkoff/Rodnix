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
 * Замок — свой, на объект, и он несёт нагрузку: объект разделяется между
 * картами (общий mmap файла, наследование при fork), а замки карт — разные.
 * Ввод-вывод под ним не делается: пейджер читает страницу до того, как она
 * вставляется в индекс.
 */

#ifndef _RODNIX_VM_OBJECT_H
#define _RODNIX_VM_OBJECT_H

#include <stdint.h>
#include <bsd/sys/queue.h>
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
    /* Узел глобального реестра объектов. Возврат памяти ходит по объектам,
     * а не по страницам: у страницы указатель на объект нельзя безопасно
     * разыменовать без гарантии, что объект не умирает прямо сейчас, — а
     * реестр даёт такую гарантию (снятие с учёта — первый шаг разрушения,
     * под замком реестра, и там же живёт try_ref). */
    TAILQ_ENTRY(vm_object) registry_link;
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
/* Вставить страницу, если место pindex ещё свободно; иначе вернуть занявшую.
 * Проверка и вставка — одна критическая секция: два отказа на одном
 * разделяемом объекте не могут вставить две разные страницы. Возвращает
 * физадрес страницы, резидентной по pindex после вызова (свою или чужую);
 * 0 — ошибка. Замещений не бывает: страница объекта по данному pindex
 * неизменна до смерти объекта. */
uint64_t vm_object_insert_or_get_page(vm_object_t* obj, uint64_t page_index, uint64_t phys);
uint32_t vm_object_resident_count(vm_object_t* obj);

/*
 * Возврат памяти (этап 8): пройти по объектам класса vm_class и отдать
 * аллокатору до target страниц, у которых единственный держатель — сам
 * объект (нигде не отображены, не закреплены). Файловые страницы с
 * материализованным задником (fb->data) переписываются в него перед
 * освобождением; VOLATILE и CACHE выбрасываются по контракту без записи;
 * анонимные и demand-paging файловые пропускаются — без свопа и грязных
 * битов их не отдать честно. Возвращает число освобождённых страниц.
 */
uint32_t vm_object_reclaim_class(uint8_t vm_class, uint32_t target);

#endif /* _RODNIX_VM_OBJECT_H */
