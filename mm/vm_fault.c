/**
 * @file vm_fault.c
 * @brief Путь отказа страницы поверх vm_map / vm_object / vm_page / pmap.
 *
 * Три фазы, и у каждой свой владелец:
 *
 *   1. Валидация — под замком карты (его берёт vm_fault_handle): запись
 *      существует, доступ разрешён.
 *   2. Разрешение страницы — объект отвечает под своим замком; наполнение
 *      от пейджера (ввод-вывод!) выполняется без единого спинлока, до
 *      публикации страницы в индексе.
 *   3. Отображение — pmap под своим замком.
 *
 * Гонка, которую закрывает эта форма: два отказа на одной странице одного
 * разделяемого объекта из двух разных карт. Замки карт разные, поэтому оба
 * доходят до объекта; проверка и вставка в объекте — одна критическая
 * секция (vm_object_insert_or_get_page), так что второй не вставляет свою
 * страницу, а принимает чужую. Раньше здесь были get и set по отдельности,
 * и два процесса, загрузившие один ELF, могли получить две разные копии
 * одной страницы файла.
 */

#include "vm_fault.h"
#include "vm_map.h"
#include "vm_pager.h"
#include "vm_page.h"
#include "pmap.h"
#include "../kernel/arch/config.h"
#include "../include/common.h"
#include "../include/console.h"
#include "../include/error.h"
#include "vm_reclaim.h"
#include "../kernel/core/oom.h"
#include "../sched/scheduler.h"

static vm_fault_stats_t g_fault_stats;

#define FAULT_COUNT(field) \
    __atomic_fetch_add(&g_fault_stats.field, 1ull, __ATOMIC_RELAXED)

void vm_fault_get_stats(vm_fault_stats_t* out)
{
    if (!out) {
        return;
    }
    out->faults     = __atomic_load_n(&g_fault_stats.faults, __ATOMIC_RELAXED);
    out->zero_fill  = __atomic_load_n(&g_fault_stats.zero_fill, __ATOMIC_RELAXED);
    out->pager_read = __atomic_load_n(&g_fault_stats.pager_read, __ATOMIC_RELAXED);
    out->cow_copy   = __atomic_load_n(&g_fault_stats.cow_copy, __ATOMIC_RELAXED);
    out->adopted    = __atomic_load_n(&g_fault_stats.adopted, __ATOMIC_RELAXED);
    out->spurious   = __atomic_load_n(&g_fault_stats.spurious, __ATOMIC_RELAXED);
}

/*
 * Выделение страницы под отказ — с полным протоколом нехватки: возврат по
 * классам, затем убийство по полосам с окном на сброс, затем ещё попытки.
 * Ноль — только когда сделано всё: либо жертва — сам проситель (ему положен
 * честный отказ), либо освобождать больше нечего.
 */
static uint64_t vm_fault_alloc_page(task_t* task)
{
    /* Терпение соразмерно жнецу: память жертвы возвращается только после
     * того, как жнец переработает её потоки, а он берёт мёртвых не раньше
     * REAP_GRACE_TICKS — при 100 Гц это больше секунды. Сдаваться раньше —
     * убивать просителя за то, что сборка мусора не мгновенна. */
    for (int attempt = 0; attempt < 60; attempt++) {
        if (task && task->doomed) {
            /* Задача приговорена: цикл нехватки не имеет права пережить
             * приговор. Немедленный отказ — и поток умрёт штатным путём,
             * отпустив замок карты; иначе жертва OOM крутилась в отказе
             * неубиваемой, а убийца выбирал её снова и снова. */
            return 0;
        }
        uint64_t phys = vm_pager_alloc_zero_page();
        if (phys) {
            return phys;
        }
        if (vm_reclaim_run(64) != 0) {
            continue;
        }
        if (!oom_kill_step()) {
            return 0;
        }
        /* Окно жертвы и её разборка идут в чужих контекстах: подождать. */
        scheduler_sleep(50);
    }
    return 0;
}

/*
 * Вход в pmap — с тем же протоколом нехватки, что и выделение данных:
 * промежуточной таблице страниц тоже нужна физическая страница, и отказ в
 * ней при пустом аллокаторе не отличается для просителя от отказа в данных.
 * Без этого исчерпание убивало процесс (включая init) мгновенно, минуя
 * возврат и убийцу по полосам.
 */
static int vm_fault_enter(task_t* task, uint64_t va, uint64_t phys,
                          uint32_t prot, uint32_t flags)
{
    int rc = RDNX_E_GENERIC;
    for (int attempt = 0; attempt < 60; attempt++) {
        if (task && task->doomed) {
            return RDNX_E_NOMEM;
        }
        rc = pmap_enter(task->address_space, va, phys, prot, flags);
        if (rc == RDNX_OK) {
            return rc;
        }
        if (vm_reclaim_run(64) != 0) {
            continue;
        }
        if (!oom_kill_step()) {
            return rc;
        }
        scheduler_sleep(50);
    }
    return rc;
}

static int vm_entry_uses_private_object_cow(const vm_map_entry_t* e)
{
    return e && e->object && (e->flags & VM_MAP_F_PRIVATE) != 0;
}

/*
 * Фаза «копия при записи»: у отображения уже есть страница, но она общая.
 * Пишущий получает собственную копию; общая теряет одну ссылку.
 */
static int vm_fault_cow_copy(task_t* task, const vm_map_entry_t* e,
                             uint64_t va, uint64_t shared_phys)
{
    uint64_t new_phys = vm_fault_alloc_page(task);
    if (!new_phys) {
        return RDNX_E_NOMEM;
    }
    memcpy(ARCH_PHYS_TO_VIRT(new_phys), ARCH_PHYS_TO_VIRT(shared_phys),
           VM_PAGE_SIZE);
    int erc = vm_fault_enter(task, va, new_phys, e->prot, PMAP_ENTER_USER);
    if (erc != RDNX_OK) {
        (void)vm_page_drop(new_phys);
        return erc;
    }
    (void)vm_page_drop(shared_phys); /* ссылка этого отображения на общую */
    vm_page_activate(new_phys);
    FAULT_COUNT(cow_copy);
    return RDNX_OK;
}

/*
 * Фаза «страницы нет»: найти её в объекте или создать.
 *
 * Приватная запись в файловое отображение — единственный случай, когда
 * страница остаётся личной и в объект не публикуется: пишущий обязан
 * получить свой экземпляр, а не править общий кэш файла.
 */
static int vm_fault_page_absent(task_t* task, vm_map_entry_t* e,
                                uint64_t va, int is_write)
{
    uint64_t phys = 0;
    uint64_t pindex = 0;
    int private_write = is_write && vm_entry_uses_private_object_cow(e);

    if (e->object) {
        pindex = (e->object_offset + (va - e->start)) / VM_PAGE_SIZE;
        phys = vm_object_get_resident_page(e->object, pindex);
        if (phys) {
            /* Индекс объекта — только на пополнение: страница не может
             * покинуть его, пока запись держит ссылку на объект, поэтому
             * взять ссылку после поиска безопасно. */
            (void)vm_page_hold(phys);
        }
    }

    if (!phys) {
        phys = vm_fault_alloc_page(task);
        if (!phys) {
            return RDNX_E_NOMEM;
        }
        if (e->object && e->object->type == VM_OBJECT_FILE) {
            vm_pager_fill_page(e->object,
                               e->object_offset + (va - e->start), phys);
            FAULT_COUNT(pager_read);
        } else {
            FAULT_COUNT(zero_fill);
        }

        if (e->object && !private_write) {
            uint64_t winner =
                vm_object_insert_or_get_page(e->object, pindex, phys);
            if (winner != 0 && winner != phys) {
                /* Гонка вставки проиграна: чужая страница уже в объекте.
                 * Свою — отдать, чужую — держать за это отображение. Оба
                 * отображения показывают на одну страницу, как и положено
                 * разделяемому объекту. */
                (void)vm_page_hold(winner);
                (void)vm_page_drop(phys);
                phys = winner;
                FAULT_COUNT(adopted);
            }
        }
    }

    uint32_t eff_prot = e->prot;
    if (!is_write && vm_entry_uses_private_object_cow(e) &&
        (eff_prot & VM_PROT_WRITE)) {
        /* Приватная файловая страница обязана снова отказать на первой
         * записи: пишущий получит свою анонимную копию, а не будет править
         * общую страницу объекта. */
        eff_prot &= ~VM_PROT_WRITE;
    }
    int rc = vm_fault_enter(task, va, phys, eff_prot, PMAP_ENTER_USER);
    if (rc == RDNX_OK) {
        /* Установленная страница встаёт в ACTIVE — очередь, по которой
         * пойдёт возврат памяти (этап 8). Закреплённых это не касается:
         * activate не трогает страницу с wire_count. */
        vm_page_activate(phys);
    }
    return rc;
}

static int vm_fault_handle_locked(task_t* task, uint64_t fault_addr,
                                  uint64_t err_code, uint64_t rip)
{
    (void)rip;
    if (!task || !task->vm_map || !task->address_space) {
        return RDNX_E_NOTFOUND;
    }
    if (fault_addr < 0x1000 || fault_addr >= ARCH_KERNEL_VIRT_BASE) {
        return RDNX_E_DENIED;
    }

    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t va = fault_addr & ~(VM_PAGE_SIZE - 1u);
    vm_map_entry_t* e = vm_map_lookup(map, va);
    if (!e) {
        return RDNX_E_NOTFOUND;
    }

    int is_write = (err_code & (1u << 1)) != 0;
    int is_exec = (err_code & (1u << 4)) != 0;
    if (is_exec && (e->prot & VM_PROT_EXEC) == 0) {
        return RDNX_E_DENIED;
    }
    if (is_write && (e->prot & VM_PROT_WRITE) == 0) {
        return RDNX_E_DENIED;
    }
    if (!is_write && !is_exec && (e->prot & VM_PROT_READ) == 0 &&
        (e->prot & VM_PROT_WRITE) == 0) {
        return RDNX_E_DENIED;
    }

    /*
     * Отказ пришёл от потока реального времени — значит, обещание этапа 7
     * для этой памяти не было дано заранее, и это страховочная сетка, а не
     * норма: mlock обязан был привести всё раньше. Сетка делает две вещи,
     * как у XNU (vmp_realtime / vm_pageout_protect_realtime): объект
     * повышается до класса RT, а страница закрепляется — возврат её больше
     * не тронет.
     */
    thread_t* self = thread_get_current();
    int rt = self && self->sched_class == SCHED_CLASS_REALTIME;
    if (rt) {
        /* Громко и безусловно: RT-поток не должен был сюда попасть вовсе —
         * обещание этапа 7 для этой памяти либо не давалось, либо сломано.
         * Печать — часть механизма, как защита RT-страниц у XNU, а не
         * отладка: молчаливая страховочная сетка скрывала бы поломку. */
        kprintf("[VMRT] rt fault tid=%llu va=%llx err=%llx entry=[%llx..%llx) flags=%x extract=%llx\n",
                (unsigned long long)self->thread_id,
                (unsigned long long)fault_addr,
                (unsigned long long)err_code,
                (unsigned long long)e->start,
                (unsigned long long)e->end,
                (unsigned)e->flags,
                (unsigned long long)pmap_extract(task->address_space, va));
        pmap_debug_dump(task->address_space, va);
        if (e->object && e->object->vm_class > (uint8_t)VM_CLASS_RT) {
            e->object->vm_class = (uint8_t)VM_CLASS_RT;
        }
    }

    uint64_t current_phys = pmap_extract(task->address_space, va);

    if (current_phys != 0 && is_write &&
        ((e->flags & VM_MAP_F_COW) || vm_entry_uses_private_object_cow(e))) {
        int crc = vm_fault_cow_copy(task, e, va, current_phys);
        if (crc == RDNX_OK && rt) {
            (void)vm_page_wire(pmap_extract(task->address_space, va));
        }
        return crc;
    }

    if (current_phys == 0) {
        int arc = vm_fault_page_absent(task, e, va, is_write);
        if (arc == RDNX_OK && rt) {
            (void)vm_page_wire(pmap_extract(task->address_space, va));
        }
        return arc;
    }

    /*
     * Отображение стоит, и доступ записью разрешён — значит, отказ уже
     * решён кем-то между тем, как процессор его взял, и тем, как этот
     * обработчик посмотрел. На нескольких процессорах это обычный исход,
     * а не ошибка: двое трогают страницу, оба отказывают, первый ставит
     * отображение, второй приходит к готовому. Успех повторяет
     * инструкцию, которая теперь находит трансляцию.
     */
    FAULT_COUNT(spurious);
    return RDNX_OK;
}

int vm_fault_handle(task_t* task, uint64_t fault_addr, uint64_t err_code, uint64_t rip)
{
    if (!task || !task->vm_map) {
        return RDNX_E_NOTFOUND;
    }
    FAULT_COUNT(faults);
    thread_t* self = thread_get_current();
    if (self) {
        self->fault_count++;
    }
    /* Замок карты — спящий, и это здесь главное: пейджер под отказом читает
     * с диска, а спать на спинлоке с выключенной преемпцией мы уже мерили.
     * Спать в обработчике отказа законно — этот путь уже спит на Giant. */
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    int _r = vm_fault_handle_locked(task, fault_addr, err_code, rip);
    vm_map_unlock(map);
    return _r;
}

/*
 * Привести и закрепить диапазон. Ходит той же машинерией, что и отказ, —
 * только заранее и с намерением записи там, где запись разрешена: COW
 * рвётся сейчас, иначе первая запись после mlock взяла бы отказ и обещание
 * было бы ложью.
 */
static int vm_task_mlock_locked(task_t* task, uint64_t addr, uint64_t len)
{
    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t s = addr & ~(VM_PAGE_SIZE - 1u);
    uint64_t e_addr = (addr + len + VM_PAGE_SIZE - 1u) & ~(VM_PAGE_SIZE - 1u);
    if (e_addr <= s) {
        return RDNX_E_INVALID;
    }

    for (uint64_t va = s; va < e_addr; va += VM_PAGE_SIZE) {
        vm_map_entry_t* e = vm_map_lookup(map, va);
        if (!e) {
            return RDNX_E_NOTFOUND;
        }
        int want_write = (e->prot & VM_PROT_WRITE) != 0;
        uint64_t phys = pmap_extract(task->address_space, va);

        if (phys != 0 && want_write &&
            ((e->flags & VM_MAP_F_COW) || vm_entry_uses_private_object_cow(e))) {
            int rc = vm_fault_cow_copy(task, e, va, phys);
            if (rc != RDNX_OK) {
                return rc;
            }
            phys = pmap_extract(task->address_space, va);
        }
        if (phys == 0) {
            int rc = vm_fault_page_absent(task, e, va, want_write);
            if (rc != RDNX_OK) {
                return rc;
            }
            phys = pmap_extract(task->address_space, va);
        }
        if (phys == 0) {
            return RDNX_E_GENERIC;
        }
        (void)vm_page_wire(phys);
    }
    return RDNX_OK;
}

int vm_task_mlock(task_t* task, uint64_t addr, uint64_t len)
{
    if (!task || !task->vm_map || !task->address_space || len == 0) {
        return RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    int _r = vm_task_mlock_locked(task, addr, len);
    vm_map_unlock(map);
    return _r;
}

int vm_task_munlock(task_t* task, uint64_t addr, uint64_t len)
{
    if (!task || !task->vm_map || !task->address_space || len == 0) {
        return RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    uint64_t s = addr & ~(VM_PAGE_SIZE - 1u);
    uint64_t e_addr = (addr + len + VM_PAGE_SIZE - 1u) & ~(VM_PAGE_SIZE - 1u);
    for (uint64_t va = s; va < e_addr; va += VM_PAGE_SIZE) {
        uint64_t phys = pmap_extract(task->address_space, va);
        if (phys) {
            (void)vm_page_unwire(phys);
        }
    }
    vm_map_unlock(map);
    return RDNX_OK;
}
