/**
 * @file vm_reclaim.c
 * @brief Возврат памяти по классам и учёт (этап 8).
 *
 * RAM — не склад, а ресурс задержки: возврат идёт в порядке цены ошибки,
 * а не возраста страниц. Сначала то, что объявлено воспроизводимым
 * (VOLATILE — выбросить, ввода-вывода нет), затем кэши, затем обычное
 * файловое с записью в задник. Открытые проекты и RT — вне игры.
 *
 * v0 берёт только страницы, у которых единственный держатель — объект:
 * нигде не отображённые и не закреплённые. Отобрать отображённую страницу
 * честно нельзя без обратных отображений (pv_list, sys/amd64 pmap), а они
 * отложены — см. открытые вопросы mm_redesign.md.
 */

#include "vm_reclaim.h"
#include "vm_object.h"
#include "vm_map.h"
#include "vm_page.h"
#include "pmap.h"
#include "../kernel/arch/pmm.h"
#include "../include/error.h"

int vm_pressure_level(void)
{
    uint64_t total = pmm_total_pages_count();
    uint64_t freep = pmm_free_pages_count();
    if (total == 0) {
        return VM_PRESSURE_NONE;
    }
    if (freep * 20u < total) {        /* < 5% */
        return VM_PRESSURE_CRITICAL;
    }
    if (freep * 100u < total * 15u) { /* < 15% */
        return VM_PRESSURE_WARN;
    }
    return VM_PRESSURE_NONE;
}

uint32_t vm_reclaim_run(uint32_t target)
{
    static const uint8_t order[] = {
        (uint8_t)VM_CLASS_VOLATILE,
        (uint8_t)VM_CLASS_CACHE,
        (uint8_t)VM_CLASS_NORMAL,
    };
    uint32_t freed = 0;
    for (unsigned i = 0; i < sizeof(order) && freed < target; i++) {
        freed += vm_object_reclaim_class(order[i], target - freed);
    }
    return freed;
}

void vm_task_mem_account(task_t* task, uint64_t* charged, uint64_t* reclaimable)
{
    uint64_t ch = 0;
    uint64_t rc = 0;
    if (charged) {
        *charged = 0;
    }
    if (reclaimable) {
        *reclaimable = 0;
    }
    if (!task || !task->vm_map || !task->address_space) {
        return;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    vm_map_entry_t* e;
    TAILQ_FOREACH(e, &map->entries, link) {
        int sole_object_holder =
            (e->object != NULL &&
             __atomic_load_n(&e->object->ref_count, __ATOMIC_ACQUIRE) == 1u);
        for (uint64_t va = e->start; va < e->end; va += VM_PAGE_SIZE) {
            uint64_t phys = pmap_extract(task->address_space, va);
            if (!phys) {
                continue;
            }
            ch++;
            uint32_t refs = vm_page_refs(phys);
            vm_page_t* m = vm_page_lookup(phys);
            int in_object = (m && m->object != NULL);
            /* Последний держатель: приватная страница с единственной
             * ссылкой (этого отображения), либо страница объекта, который
             * держит только эта запись, при ссылках «объект + это
             * отображение». Оценка, а не теорема — но ошибается в
             * осторожную сторону. */
            if ((!in_object && refs == 1u) ||
                (in_object && sole_object_holder && refs == 2u)) {
                rc++;
            }
        }
    }
    vm_map_unlock(map);
    if (charged) {
        *charged = ch;
    }
    if (reclaimable) {
        *reclaimable = rc;
    }
}
