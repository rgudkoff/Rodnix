#include "vm_object.h"
#include "vm_page.h"
#include "../lib/heap.h"
#include "../kernel/arch/config.h"
#include "../include/common.h"
#include "../include/debug.h"
#include "../include/console.h"
#include "../include/error.h"

/* Реестр всех живых объектов. Снятие с учёта — первый шаг разрушения. */
static spinlock_t vm_object_registry_spin;
static TAILQ_HEAD(vm_object_registry_head, vm_object) vm_object_registry =
    TAILQ_HEAD_INITIALIZER(vm_object_registry);

/* Взять ссылку, только если объект ещё жив. Ноль — разрушение уже идёт. */
static int vm_object_try_ref(vm_object_t* obj)
{
    uint32_t old = __atomic_load_n(&obj->ref_count, __ATOMIC_ACQUIRE);
    for (;;) {
        if (old == 0) {
            return 0;
        }
        if (__atomic_compare_exchange_n(&obj->ref_count, &old, old + 1u, true,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            return 1;
        }
    }
}

static uint64_t vm_object_align_up(uint64_t value)
{
    return (value + VM_OBJECT_PAGE_SIZE - 1u) & ~(VM_OBJECT_PAGE_SIZE - 1u);
}

/*
 * Мультипликативный хеш по pindex. Смежные индексы — обычный случай
 * (последовательное чтение файла), и им нельзя попадать в одну корзину;
 * умножение на золотое сечение рассеивает соседей по всей таблице.
 */
static inline uint32_t vm_object_bucket(const vm_object_t* obj, uint32_t pindex)
{
    return (uint32_t)(((uint64_t)pindex * 0x9E3779B97F4A7C15ULL) >> 32)
           & (obj->nbuckets - 1u);
}

/* Найти страницу по pindex. Вызывающий держит obj->lock; nbuckets != 0. */
static vm_page_t* vm_object_lookup_locked(const vm_object_t* obj, uint32_t pindex)
{
    uint32_t idx = obj->buckets[vm_object_bucket(obj, pindex)];
    while (idx != VM_PAGE_NIL) {
        vm_page_t* m = vm_page_at_index(idx);
        if (m->pindex == pindex) {
            return m;
        }
        idx = m->obj_next;
    }
    return NULL;
}

/* Вставить страницу. Вызывающий держит obj->lock, место есть. */
static void vm_object_link_locked(vm_object_t* obj, vm_page_t* m, uint32_t pindex)
{
    if (m->object != NULL) {
        /* Страница ещё числится в чьём-то индексе — связывание перешьёт
         * obj_next и разорвёт ту цепочку. Единственный честный путь сюда —
         * повторное использование освобождённой страницы, которую забыли
         * вынуть, то есть чужой двойной drop. Громко и безусловно. */
        kprintf("[VMOBJ] relink page idx=%llu: owner=%p pindex=%u -> obj=%p pindex=%u\n",
                (unsigned long long)vm_page_index(m),
                (void*)m->object, m->pindex, (void*)obj, pindex);
    }
    uint32_t* slot = &obj->buckets[vm_object_bucket(obj, pindex)];
    m->object = obj;
    m->pindex = pindex;
    m->obj_next = *slot;
    *slot = (uint32_t)vm_page_index(m);
    obj->resident++;
}

/*
 * Обеспечить ёмкость под ещё одну страницу.
 *
 * Выделение — вне замка: kmalloc берёт замок кучи, и держать поверх него
 * ещё и объектный незачем. Взяв замок обратно, проверяем, не вырос ли
 * индекс чужими руками, — при нашем вводе «рядом со старым» все вызовы пока
 * сериализованы глобальным vm_lock, но код пишется под замок объекта,
 * который останется, когда vm_lock уйдёт (этап 5).
 *
 * Перенос цепочек ограничен числом резидентных страниц, и рост в четыре
 * раза делает его амортизированно дешёвым.
 */
static int vm_object_ensure_capacity(vm_object_t* obj)
{
    for (;;) {
        uint64_t f = spinlock_lock_irqsave(&obj->lock);
        uint32_t nb = obj->nbuckets;
        uint32_t res = obj->resident;
        spinlock_unlock_irqrestore(&obj->lock, f);

        if (nb != 0 && res + 1u <= nb * 2u) {
            return RDNX_OK;
        }

        uint32_t newn = nb ? nb * 4u : 8u;
        uint32_t* nbk = (uint32_t*)kmalloc((size_t)newn * sizeof(uint32_t));
        if (!nbk) {
            return RDNX_E_NOMEM;
        }
        for (uint32_t i = 0; i < newn; i++) {
            nbk[i] = VM_PAGE_NIL;
        }

        f = spinlock_lock_irqsave(&obj->lock);
        if (obj->nbuckets != nb) {
            /* Кто-то успел раньше; наша таблица не нужна. */
            spinlock_unlock_irqrestore(&obj->lock, f);
            kfree(nbk);
            continue;
        }
        uint32_t* old = obj->buckets;
        uint32_t oldn = obj->nbuckets;
        obj->buckets = nbk;
        obj->nbuckets = newn;
        for (uint32_t b = 0; b < oldn; b++) {
            uint32_t idx = old[b];
            while (idx != VM_PAGE_NIL) {
                vm_page_t* m = vm_page_at_index(idx);
                uint32_t next = m->obj_next;
                uint32_t* slot = &obj->buckets[vm_object_bucket(obj, m->pindex)];
                m->obj_next = *slot;
                *slot = idx;
                idx = next;
            }
        }
        spinlock_unlock_irqrestore(&obj->lock, f);
        if (old) {
            kfree(old);
        }
    }
}

vm_object_t* vm_object_create(vm_object_type_t type, uint64_t size)
{
    uint64_t aligned = vm_object_align_up(size ? size : VM_OBJECT_PAGE_SIZE);
    uint64_t pages = aligned / VM_OBJECT_PAGE_SIZE;
    /* pindex хранится в vm_page как uint32: потолок объекта — 16 ТБ. Не
     * ограничение, а признание: объект больше физической памяти на порядки
     * означает ошибку вызывающего, и лучше отказать здесь, чем молча резать
     * индекс. */
    if (pages > 0xFFFFFFFFull) {
        return NULL;
    }

    vm_object_t* obj = (vm_object_t*)kmalloc(sizeof(vm_object_t));
    if (!obj) {
        return NULL;
    }
    memset(obj, 0, sizeof(*obj));
    obj->type = type;
    obj->vm_class = (uint8_t)VM_CLASS_NORMAL;
    obj->size = size;
    obj->page_count = pages;
    spinlock_init(&obj->lock);
    /* Индекс — лениво, при первой вставке: объект без резидентных страниц
     * (отложенный mmap, который так и не тронули) не стоит ничего. */
    obj->ref_count = 1;
    uint64_t rf = spinlock_lock_irqsave(&vm_object_registry_spin);
    TAILQ_INSERT_TAIL(&vm_object_registry, obj, registry_link);
    spinlock_unlock_irqrestore(&vm_object_registry_spin, rf);
    return obj;
}

void vm_object_ref(vm_object_t* obj)
{
    if (!obj) {
        return;
    }
    __atomic_fetch_add(&obj->ref_count, 1u, __ATOMIC_RELAXED);
}

/*
 * Разрушение: пройти индекс, для файлового объекта вернуть содержимое в
 * задник, отдать страницы. Идём по корзинам — порядок страниц не важен,
 * каждая пишется в своё смещение.
 */
static void vm_object_destroy(vm_object_t* obj)
{
    /* Сначала — с учёта: после этой строки возврат памяти объект не найдёт,
     * а try_ref для опоздавших уже отказывает по нулевому счётчику. */
    uint64_t rf = spinlock_lock_irqsave(&vm_object_registry_spin);
    TAILQ_REMOVE(&vm_object_registry, obj, registry_link);
    spinlock_unlock_irqrestore(&vm_object_registry_spin, rf);

    vm_file_backing_t* fb = (vm_file_backing_t*)obj->pager_private;
    bool writeback = (obj->type == VM_OBJECT_FILE && fb && fb->data && fb->size > 0);

    for (uint32_t b = 0; b < obj->nbuckets; b++) {
        uint32_t idx = obj->buckets[b];
        while (idx != VM_PAGE_NIL) {
            vm_page_t* m = vm_page_at_index(idx);
            uint32_t next = m->obj_next;
            uint64_t phys = vm_page_phys(m);
            if (writeback) {
                uint64_t off = fb->file_offset
                             + (uint64_t)m->pindex * VM_OBJECT_PAGE_SIZE;
                if (off < fb->size) {
                    uint64_t avail = fb->size - off;
                    uint64_t copy = (avail > VM_OBJECT_PAGE_SIZE)
                                        ? VM_OBJECT_PAGE_SIZE : avail;
                    memcpy((uint8_t*)fb->data + off,
                           ARCH_PHYS_TO_VIRT(phys), (size_t)copy);
                }
            }
            m->object = NULL;
            m->obj_next = 0;
            (void)vm_page_drop(phys); /* Ссылка владения объекта. */
            idx = next;
        }
    }
    if (obj->buckets) {
        kfree(obj->buckets);
        obj->buckets = NULL;
    }
    if (obj->pager_private) {
        vm_file_backing_t* fb2 = (vm_file_backing_t*)obj->pager_private;
        if (fb2->pager_priv) {
            kfree(fb2->pager_priv);
        }
        kfree(obj->pager_private);
        obj->pager_private = NULL;
    }
    kfree(obj);
}

void vm_object_unref(vm_object_t* obj)
{
    if (!obj) {
        return;
    }
    uint32_t old = __atomic_fetch_sub(&obj->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (old == 0) {
        DEBUG_WARN("vm_object: unref past zero");
        __atomic_store_n(&obj->ref_count, 0u, __ATOMIC_RELAXED);
        return;
    }
    if (old == 1) {
        /* Последняя ссылка была наша: конкурентов у объекта больше нет,
         * замок не нужен. */
        vm_object_destroy(obj);
    }
}

uint64_t vm_object_get_resident_page(vm_object_t* obj, uint64_t page_index)
{
    if (!obj || page_index >= obj->page_count) {
        return 0;
    }
    uint64_t f = spinlock_lock_irqsave(&obj->lock);
    vm_page_t* m = (obj->nbuckets != 0)
                       ? vm_object_lookup_locked(obj, (uint32_t)page_index)
                       : NULL;
    uint64_t phys = m ? vm_page_phys(m) : 0;
    spinlock_unlock_irqrestore(&obj->lock, f);
    return phys;
}

uint64_t vm_object_insert_or_get_page(vm_object_t* obj, uint64_t page_index, uint64_t phys)
{
    if (!obj || page_index >= obj->page_count || !phys) {
        return 0;
    }
    vm_page_t* m = vm_page_lookup(phys);
    if (!m) {
        return 0;
    }

    if (vm_object_ensure_capacity(obj) != RDNX_OK) {
        return 0;
    }

    /* Ссылка владения берётся до публикации: в индексе не бывает страницы,
     * которую объект не держит. Проигранная гонка отдаёт её обратно. */
    (void)vm_page_hold(phys);

    uint64_t f = spinlock_lock_irqsave(&obj->lock);
    vm_page_t* old = vm_object_lookup_locked(obj, (uint32_t)page_index);
    if (old) {
        uint64_t winner = vm_page_phys(old);
        spinlock_unlock_irqrestore(&obj->lock, f);
        (void)vm_page_drop(phys);
        return winner;
    }
    vm_object_link_locked(obj, m, (uint32_t)page_index);
    spinlock_unlock_irqrestore(&obj->lock, f);
    return phys;
}

uint32_t vm_object_resident_count(vm_object_t* obj)
{
    if (!obj) {
        return 0;
    }
    uint64_t f = spinlock_lock_irqsave(&obj->lock);
    uint32_t n = obj->resident;
    spinlock_unlock_irqrestore(&obj->lock, f);
    return n;
}

/* Вынуть страницу из цепочки её корзины. Вызывающий держит obj->lock. */
static void vm_object_unlink_locked(vm_object_t* obj, vm_page_t* m)
{
    uint32_t* slot = &obj->buckets[vm_object_bucket(obj, m->pindex)];
    while (*slot != VM_PAGE_NIL) {
        vm_page_t* cur = vm_page_at_index(*slot);
        if (cur == m) {
            *slot = m->obj_next;
            m->obj_next = 0;
            m->object = NULL;
            obj->resident--;
            return;
        }
        slot = &cur->obj_next;
    }
    DEBUG_WARN("vm_object: page pindex=%u not in its bucket", m->pindex);
}

/* Отдать до target одиноких страниц одного объекта. Вызывающий держит
 * ссылку на объект. Возвращает число освобождённых. */
static uint32_t vm_object_reclaim_pages(vm_object_t* obj, uint32_t target)
{
    vm_file_backing_t* fb = (vm_file_backing_t*)obj->pager_private;
    bool has_backing = (obj->type == VM_OBJECT_FILE && fb && fb->data && fb->size > 0);
    bool discardable = (obj->vm_class == (uint8_t)VM_CLASS_VOLATILE ||
                        obj->vm_class == (uint8_t)VM_CLASS_CACHE);

    /* Без свопа честно отдать можно: выбрасываемое по контракту — всегда;
     * файловое с материализованным задником — с записью обратно. Анонимное
     * и demand-paging файловое пропускаются: терять данные нельзя, а
     * грязных битов, чтобы отличить чистое, у нас пока нет. */
    if (!discardable && !has_backing) {
        return 0;
    }

    uint32_t freed = 0;
    uint64_t victims[32];
    while (freed < target) {
        uint32_t nv = 0;
        uint64_t f = spinlock_lock_irqsave(&obj->lock);
        for (uint32_t b = 0; b < obj->nbuckets && nv < 32; b++) {
            uint32_t idx = obj->buckets[b];
            while (idx != VM_PAGE_NIL && nv < 32) {
                vm_page_t* m = vm_page_at_index(idx);
                uint32_t next = m->obj_next;
                if (m->wire_count == 0 &&
                    __atomic_load_n(&m->ref_count, __ATOMIC_ACQUIRE) == 1u) {
                    uint64_t phys = vm_page_phys(m);
                    if (!discardable && has_backing) {
                        uint64_t off = fb->file_offset +
                            (uint64_t)m->pindex * VM_OBJECT_PAGE_SIZE;
                        if (off < fb->size) {
                            uint64_t avail = fb->size - off;
                            uint64_t copy = (avail > VM_OBJECT_PAGE_SIZE)
                                                ? VM_OBJECT_PAGE_SIZE : avail;
                            memcpy((uint8_t*)fb->data + off,
                                   ARCH_PHYS_TO_VIRT(phys), (size_t)copy);
                        }
                    }
                    vm_object_unlink_locked(obj, m);
                    victims[nv++] = phys;
                }
                idx = next;
            }
        }
        spinlock_unlock_irqrestore(&obj->lock, f);
        if (nv == 0) {
            break;
        }
        for (uint32_t i = 0; i < nv; i++) {
            (void)vm_page_drop(victims[i]);  /* последняя ссылка: страница свободна */
        }
        freed += nv;
    }
    return freed;
}

uint32_t vm_object_reclaim_class(uint8_t vm_class, uint32_t target)
{
    uint32_t freed = 0;
    /* Обход с рестартом: под замком реестра выбирается один объект и
     * берётся try_ref; работа — без замка реестра; unref может разрушить
     * объект, поэтому позиция в списке не переживает итерацию, и обход
     * помечает пройденное по указателю с начала. Реестр короткий, а
     * возврат — редкое событие; квадрат здесь дешевле корректной
     * курсорной механики. */
    vm_object_t* last = NULL;
    while (freed < target) {
        vm_object_t* pick = NULL;
        uint64_t rf = spinlock_lock_irqsave(&vm_object_registry_spin);
        vm_object_t* it;
        int past_last = (last == NULL);
        TAILQ_FOREACH(it, &vm_object_registry, registry_link) {
            if (!past_last) {
                if (it == last) {
                    past_last = 1;
                }
                continue;
            }
            if (it->vm_class == vm_class && it->resident != 0 &&
                vm_object_try_ref(it)) {
                pick = it;
                break;
            }
        }
        spinlock_unlock_irqrestore(&vm_object_registry_spin, rf);
        if (!pick) {
            /* last мог умереть и обход с него не возобновился — один
             * рестарт с начала, потом честный конец. */
            if (last != NULL) {
                last = NULL;
                continue;
            }
            break;
        }
        freed += vm_object_reclaim_pages(pick, target - freed);
        last = pick;
        vm_object_unref(pick);
    }
    return freed;
}
