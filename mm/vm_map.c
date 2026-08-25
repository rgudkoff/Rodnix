#include "vm_map.h"
#include "../kernel/core/kmutex.h"
#include "vm_pager.h"
#include "vm_page.h"
#include "pmap.h"
#include "../kernel/arch/interrupt_frame.h"
#include "../kernel/arch/config.h"
#include "../lib/heap.h"
#include "../include/console.h"
#include "../include/common.h"
#include "../include/error.h"

#define VM_USER_MIN      0x0000000000001000ULL
#define VM_USER_MAX      0x0000000080000000ULL
#define VM_DEFAULT_MMAP  0x0000000060000000ULL

static void (*g_fb_release_hook)(uint32_t display_idx) = NULL;

/*
 * Замок — на карту, и его берут все публичные входы без исключения.
 *
 * Глобальный vm_lock был спинлоком с выключенной преемпцией на всё время
 * операции — включая fork, обходящий каждую страницу родителя, и путь
 * отказа, читающий с диска. И он был дырявым: семейство vm_task_mmap* и brk
 * его вовсе не брали, так что mmap против конкурентного отказа был гонкой.
 *
 * Спящий замок на карту закрывает и то и другое: карты разных задач больше
 * не сериализуют друг друга, ожидание не жжёт процессор, а во время
 * ввода-вывода пейджера процессор занят другими потоками.
 */
void vm_map_lock(vm_map_t* map)
{
    kmutex_lock(&map->lock);
}

void vm_map_unlock(vm_map_t* map)
{
    kmutex_unlock(&map->lock);
}

void vm_set_fb_release_hook(void (*fn)(uint32_t display_idx))
{
    g_fb_release_hook = fn;
}

static inline uint64_t vm_align_down(uint64_t v)
{
    return v & ~(VM_PAGE_SIZE - 1u);
}

static inline uint64_t vm_align_up(uint64_t v)
{
    return (v + VM_PAGE_SIZE - 1u) & ~(VM_PAGE_SIZE - 1u);
}

static vm_map_t* vm_map_create(pmap_t pmap)
{
    vm_map_t* map = (vm_map_t*)kmalloc(sizeof(vm_map_t));
    if (!map) {
        return NULL;
    }
    memset(map, 0, sizeof(*map));
    map->pmap = pmap;
    TAILQ_INIT(&map->entries);
    kmutex_init(&map->lock, "vm_map");
    return map;
}

static void vm_map_destroy(vm_map_t* map)
{
    if (!map) {
        return;
    }
    vm_map_entry_t* e;
    vm_map_entry_t* next;
    TAILQ_FOREACH_SAFE(e, &map->entries, link, next) {
        TAILQ_REMOVE(&map->entries, e, link);
        if (e->object) {
            vm_object_unref(e->object);
        }
        kfree(e);
    }
    map->entry_count = 0;
    kfree(map);
}

static int vm_range_valid(uint64_t start, uint64_t end)
{
    if (start < VM_USER_MIN || end <= start || end > VM_USER_MAX) {
        return 0;
    }
    if (start >= ARCH_KERNEL_VIRT_BASE || end >= ARCH_KERNEL_VIRT_BASE) {
        return 0;
    }
    return 1;
}

static int vm_map_add(vm_map_t* map,
                      uint64_t start,
                      uint64_t len,
                      uint32_t prot,
                      uint32_t flags,
                      vm_object_t* obj,
                      uint64_t object_offset)
{
    uint64_t s = vm_align_down(start);
    uint64_t e = vm_align_up(start + len);
    if (!map || len == 0 || !vm_range_valid(s, e)) {
        return RDNX_E_INVALID;
    }

    /* Точка вставки и проверка пересечения — один проход: место новой
     * записи перед первой, что начинается не левее её конца; сосед слева
     * не должен заходить на неё. */
    vm_map_entry_t* after = NULL;   /* последняя запись со start < s */
    vm_map_entry_t* it;
    TAILQ_FOREACH(it, &map->entries, link) {
        if (it->start >= e) {
            break;
        }
        if (it->start >= s || it->end > s) {
            return RDNX_E_BUSY;   /* пересечение */
        }
        after = it;
    }

    vm_map_entry_t* ne = (vm_map_entry_t*)kmalloc(sizeof(vm_map_entry_t));
    if (!ne) {
        return RDNX_E_NOMEM;
    }
    memset(ne, 0, sizeof(*ne));
    ne->start = s;
    ne->end = e;
    ne->prot = prot;
    ne->flags = flags;
    ne->object = obj;
    ne->object_offset = object_offset;
    if (obj) {
        vm_object_ref(obj);
    }
    if (after) {
        TAILQ_INSERT_AFTER(&map->entries, after, ne, link);
    } else {
        TAILQ_INSERT_HEAD(&map->entries, ne, link);
    }
    map->entry_count++;
    return RDNX_OK;
}

static int vm_map_remove(vm_map_t* map, uint64_t start, uint64_t len, pmap_t pmap)
{
    uint64_t s = vm_align_down(start);
    uint64_t e = vm_align_up(start + len);
    if (!map || len == 0 || e <= s) {
        return RDNX_E_INVALID;
    }
    int removed = 0;
    vm_map_entry_t* cur;
    vm_map_entry_t* next;
    TAILQ_FOREACH_SAFE(cur, &map->entries, link, next) {
        if (cur->start >= e) {
            break;   /* сортировка: дальше пересечений нет */
        }
        uint64_t rs = (s > cur->start) ? s : cur->start;
        uint64_t re = (e < cur->end) ? e : cur->end;
        if (re <= rs) {
            continue;
        }

        if (pmap == map->pmap) {
            for (uint64_t va = rs; va < re; va += VM_PAGE_SIZE) {
                uint64_t phys = pmap_extract(pmap, va);
                if (phys != 0) {
                    pmap_remove(pmap, va, va + VM_PAGE_SIZE);
                    (void)vm_page_drop(phys);
                }
            }
        }
        removed = 1;

        if (rs == cur->start && re == cur->end) {
            /* Full removal: fire hook for framebuffer entries so the display
             * ownership refcount is decremented (covers both munmap and exit). */
            if ((cur->flags & VM_MAP_F_FRAMEBUFFER) && g_fb_release_hook) {
                g_fb_release_hook((uint32_t)cur->object_offset);
            }
            if (cur->object) {
                vm_object_unref(cur->object);
            }
            TAILQ_REMOVE(&map->entries, cur, link);
            kfree(cur);
            map->entry_count--;
            continue;
        }

        if (rs == cur->start) {
            cur->start = re;
            cur->object_offset += (re - rs);
            continue;
        }

        if (re == cur->end) {
            cur->end = rs;
            continue;
        }

        /* Дыра в середине: запись расщепляется. Хвост — новая запись сразу
         * за текущей, сортировка сохраняется сама. Раньше здесь был потолок
         * массива; теперь единственный отказ — нет памяти под запись. */
        vm_map_entry_t* tail = (vm_map_entry_t*)kmalloc(sizeof(vm_map_entry_t));
        if (!tail) {
            return RDNX_E_NOMEM;
        }
        memset(tail, 0, sizeof(*tail));
        tail->start = re;
        tail->end = cur->end;
        tail->prot = cur->prot;
        tail->flags = cur->flags;
        tail->object = cur->object;
        tail->object_offset = cur->object_offset + (re - cur->start);
        if (tail->object) {
            vm_object_ref(tail->object);
        }
        cur->end = rs;
        TAILQ_INSERT_AFTER(&map->entries, cur, tail, link);
        map->entry_count++;
        /* Диапазон [s,e) целиком внутри одной записи — дальше искать нечего. */
        break;
    }
    return removed ? RDNX_OK : RDNX_E_NOTFOUND;
}

vm_map_entry_t* vm_map_lookup(vm_map_t* map, uint64_t addr)
{
    if (!map) {
        return NULL;
    }
    vm_map_entry_t* e;
    TAILQ_FOREACH(e, &map->entries, link) {
        if (e->start > addr) {
            break;   /* сортировка: дальше только правее */
        }
        if (addr < e->end) {
            return e;
        }
    }
    return NULL;
}

static uint64_t vm_find_gap(vm_map_t* map, uint64_t hint, uint64_t len)
{
    uint64_t alen = vm_align_up(len);
    uint64_t s = vm_align_up(hint);
    if (alen == 0 || s + alen <= s) {
        return 0;
    }
    if (s < VM_DEFAULT_MMAP) {
        s = VM_DEFAULT_MMAP;
    }

    /* Один проход по сортированному списку вместо прощупывания адресного
     * пространства постранично: кандидат сдвигается за конец каждой записи,
     * которая ему мешает. Прежний цикл был O(диапазон × записи) и в худшем
     * случае делал сотни тысяч проверок пересечения. */
    vm_map_entry_t* e;
    TAILQ_FOREACH(e, &map->entries, link) {
        if (e->end <= s) {
            continue;             /* запись целиком левее кандидата */
        }
        if (e->start >= s + alen) {
            break;                /* кандидат помещается до этой записи */
        }
        s = vm_align_up(e->end);  /* мешает: сдвинуться за неё */
    }
    if (s + alen > VM_USER_MAX || s + alen <= s) {
        return 0;
    }
    return s;
}

static int vm_task_prepare_exec_locked(task_t* task, pmap_t user_pmap){
    if (!task || !user_pmap) {
        return RDNX_E_INVALID;
    }

    if (task->vm_map) {
        vm_map_destroy(task->vm_map);
        task->vm_map = NULL;
    }

    vm_map_t* map = vm_map_create(user_pmap);
    if (!map) {
        return RDNX_E_NOMEM;
    }
    task->vm_map = map;
    task->vm_brk_base = 0;
    task->vm_brk_end = 0;
    task->vm_mmap_base = VM_DEFAULT_MMAP;
    task->vm_mmap_hint = VM_DEFAULT_MMAP;
    return RDNX_OK;
}

int vm_task_prepare_exec(task_t* task, pmap_t user_pmap)
{
    /* Без замка: exec выполняется единственным потоком задачи, и старая
     * карта, которую он сносит, больше никому не видна. Брать замок карты,
     * которую сейчас освободим, значило бы освобождать удерживаемый kmutex. */
    return vm_task_prepare_exec_locked(task, user_pmap);
}



static int vm_task_map_fixed_locked(task_t* task, uint64_t start, uint64_t len, uint32_t prot, uint32_t flags){
    if (!task || !task->vm_map) {
        return RDNX_E_INVALID;
    }
    return vm_map_add((vm_map_t*)task->vm_map, start, len, prot, flags | VM_MAP_F_FIXED, NULL, 0);
}

int vm_task_map_fixed(task_t* task, uint64_t start, uint64_t len, uint32_t prot, uint32_t flags)
{
    if (!task || !task->vm_map) {
        return RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    int _r = vm_task_map_fixed_locked(task, start, len, prot, flags);
    vm_map_unlock(map);
    return _r;
}



static int vm_task_set_brk_base_locked(task_t* task, uint64_t brk_base){
    if (!task) {
        return RDNX_E_INVALID;
    }
    uint64_t b = vm_align_up(brk_base);
    task->vm_brk_base = b;
    task->vm_brk_end = b;
    return RDNX_OK;
}

int vm_task_set_brk_base(task_t* task, uint64_t brk_base)
{
    if (!task) {
        return RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    if (map) {
        vm_map_lock(map);
    }
    int _r = vm_task_set_brk_base_locked(task, brk_base);
    if (map) {
        vm_map_unlock(map);
    }
    return _r;
}



static long vm_task_mmap_locked(task_t* task, uint64_t addr_hint, uint64_t len, uint32_t prot, uint32_t flags)
{
    if (!task || !task->vm_map || len == 0) {
        return (long)RDNX_E_INVALID;
    }
    uint64_t alen = vm_align_up(len);
    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t addr = 0;

    if ((flags & VM_MAP_F_FIXED) != 0) {
        addr = vm_align_down(addr_hint);
        /* Fixed mappings replace overlapping ranges in place. */
        (void)vm_map_remove(map, addr, alen, task->address_space);
    } else {
        uint64_t hint = addr_hint ? addr_hint : task->vm_mmap_hint;
        addr = vm_find_gap(map, hint, alen);
    }
    if (!addr) {
        return (long)RDNX_E_NOMEM;
    }

    vm_object_t* obj = vm_object_create(VM_OBJECT_ANON, alen);
    if (!obj) {
        return (long)RDNX_E_NOMEM;
    }
    int rc = vm_map_add(map, addr, alen, prot, flags | VM_MAP_F_LAZY | VM_MAP_F_ANON, obj, 0);
    vm_object_unref(obj);
    if (rc != RDNX_OK) {
        return (long)rc;
    }
    task->vm_mmap_hint = addr + alen;
    return (long)addr;
}

/* Семейство mmap и brk раньше не брали замок VM вовсе — mmap против
 * конкурентного отказа страницы был гонкой по массиву записей. Теперь все
 * публичные входы без исключения идут под замком карты. */
long vm_task_mmap(task_t* task, uint64_t addr_hint, uint64_t len, uint32_t prot, uint32_t flags)
{
    if (!task || !task->vm_map) {
        return (long)RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    long _r = vm_task_mmap_locked(task, addr_hint, len, prot, flags);
    vm_map_unlock(map);
    return _r;
}

static long vm_task_mmap_phys_locked(task_t* task, uint64_t addr_hint, uint64_t len,
                                     uint32_t prot, uint64_t phys_base, uint32_t display_idx)
{
    if (!task || !task->vm_map || len == 0 || phys_base == 0) {
        return (long)RDNX_E_INVALID;
    }
    uint64_t alen = vm_align_up(len);
    vm_map_t* map = (vm_map_t*)task->vm_map;

    uint64_t hint = addr_hint ? addr_hint : task->vm_mmap_hint;
    uint64_t addr = vm_find_gap(map, hint, alen);
    if (!addr) {
        return (long)RDNX_E_NOMEM;
    }

    /* Register the range: VM_MAP_F_FRAMEBUFFER marks this as a physical MMIO
     * mapping; display_idx is stored in object_offset for release tracking. */
    int rc = vm_map_add(map, addr, alen, prot,
                        VM_MAP_F_PRIVATE | VM_MAP_F_FRAMEBUFFER, NULL,
                        (uint64_t)display_idx);
    if (rc != RDNX_OK) {
        return (long)rc;
    }

    /* Disable caching for MMIO / framebuffer regions. */


    for (uint64_t off = 0; off < alen; off += VM_PAGE_SIZE) {
        pmap_enter(task->address_space, addr + off, phys_base + off, prot,
                   PMAP_ENTER_USER | PMAP_ENTER_NOCACHE);
    }

    task->vm_mmap_hint = addr + alen;
    return (long)addr;
}

long vm_task_mmap_phys(task_t* task, uint64_t addr_hint, uint64_t len,
                       uint32_t prot, uint64_t phys_base, uint32_t display_idx)
{
    if (!task || !task->vm_map) {
        return (long)RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    long _r = vm_task_mmap_phys_locked(task, addr_hint, len, prot, phys_base, display_idx);
    vm_map_unlock(map);
    return _r;
}

static long vm_task_mmap_object_locked(task_t* task,
                                       uint64_t addr_hint,
                                       uint64_t len,
                                       uint32_t prot,
                                       uint32_t flags,
                                       vm_object_t* obj,
                                       uint64_t object_offset)
{
    if (!task || !task->vm_map || len == 0 || !obj) {
        return (long)RDNX_E_INVALID;
    }
    uint64_t alen = vm_align_up(len);
    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t addr = 0;

    if ((flags & VM_MAP_F_FIXED) != 0) {
        addr = vm_align_down(addr_hint);
        /* MAP_FIXED replaces existing mappings in target range. */
        (void)vm_map_remove(map, addr, alen, task->address_space);
    } else {
        uint64_t hint = addr_hint ? addr_hint : task->vm_mmap_hint;
        addr = vm_find_gap(map, hint, alen);
    }
    if (!addr) {
        return (long)RDNX_E_NOMEM;
    }
    int rc = vm_map_add(map, addr, alen, prot, flags | VM_MAP_F_LAZY, obj, object_offset);
    if (rc != RDNX_OK) {
        return (long)rc;
    }
    task->vm_mmap_hint = addr + alen;
    return (long)addr;
}

long vm_task_mmap_object(task_t* task,
                         uint64_t addr_hint,
                         uint64_t len,
                         uint32_t prot,
                         uint32_t flags,
                         vm_object_t* obj,
                         uint64_t object_offset)
{
    if (!task || !task->vm_map) {
        return (long)RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    long _r = vm_task_mmap_object_locked(task, addr_hint, len, prot, flags, obj, object_offset);
    vm_map_unlock(map);
    return _r;
}

static long vm_task_mmap_file_locked(task_t* task,
                                     uint64_t addr_hint,
                                     uint64_t len,
                                     uint32_t prot,
                                     uint32_t flags,
                                     const uint8_t* data,
                                     uint64_t data_size,
                                     uint64_t file_offset)
{
    if (!task || !task->vm_map || len == 0 || !data) {
        return (long)RDNX_E_INVALID;
    }
    uint64_t alen = vm_align_up(len);
    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t addr = 0;

    if ((flags & VM_MAP_F_FIXED) != 0) {
        addr = vm_align_down(addr_hint);
        /* MAP_FIXED replaces existing mappings in target range. */
        (void)vm_map_remove(map, addr, alen, task->address_space);
    } else {
        uint64_t hint = addr_hint ? addr_hint : task->vm_mmap_hint;
        addr = vm_find_gap(map, hint, alen);
    }
    if (!addr) {
        return (long)RDNX_E_NOMEM;
    }

    vm_object_t* obj = vm_object_create(VM_OBJECT_FILE, alen);
    if (!obj) {
        return (long)RDNX_E_NOMEM;
    }
    vm_file_backing_t* fb = (vm_file_backing_t*)kmalloc(sizeof(vm_file_backing_t));
    if (!fb) {
        vm_object_unref(obj);
        return (long)RDNX_E_NOMEM;
    }
    fb->data = data;
    fb->size = data_size;
    fb->file_offset = 0;
    obj->pager_private = fb;

    int rc = vm_map_add(map, addr, alen, prot, flags | VM_MAP_F_LAZY, obj, file_offset);
    vm_object_unref(obj);
    if (rc != RDNX_OK) {
        return (long)rc;
    }
    task->vm_mmap_hint = addr + alen;
    return (long)addr;
}

long vm_task_mmap_file(task_t* task,
                       uint64_t addr_hint,
                       uint64_t len,
                       uint32_t prot,
                       uint32_t flags,
                       const uint8_t* data,
                       uint64_t data_size,
                       uint64_t file_offset)
{
    if (!task || !task->vm_map) {
        return (long)RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    long _r = vm_task_mmap_file_locked(task, addr_hint, len, prot, flags, data, data_size, file_offset);
    vm_map_unlock(map);
    return _r;
}

static long vm_task_mmap_file_backing_locked(task_t* task,
                                             uint64_t addr_hint,
                                             uint64_t len,
                                             uint32_t prot,
                                             uint32_t flags,
                                             vm_file_backing_t* fb,
                                             uint64_t file_offset)
{
    if (!task || !task->vm_map || len == 0 || !fb) {
        return (long)RDNX_E_INVALID;
    }
    uint64_t alen = vm_align_up(len);
    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t addr = 0;

    if ((flags & VM_MAP_F_FIXED) != 0) {
        addr = vm_align_down(addr_hint);
        (void)vm_map_remove(map, addr, alen, task->address_space);
    } else {
        uint64_t hint = addr_hint ? addr_hint : task->vm_mmap_hint;
        addr = vm_find_gap(map, hint, alen);
    }
    if (!addr) {
        return (long)RDNX_E_NOMEM;
    }

    vm_object_t* obj = vm_object_create(VM_OBJECT_FILE, alen);
    if (!obj) {
        return (long)RDNX_E_NOMEM;
    }
    obj->pager_private = fb;

    int rc = vm_map_add(map, addr, alen, prot, flags | VM_MAP_F_LAZY, obj, file_offset);
    vm_object_unref(obj);
    if (rc != RDNX_OK) {
        return (long)rc;
    }
    task->vm_mmap_hint = addr + alen;
    return (long)addr;
}

long vm_task_mmap_file_backing(task_t* task,
                               uint64_t addr_hint,
                               uint64_t len,
                               uint32_t prot,
                               uint32_t flags,
                               vm_file_backing_t* fb,
                               uint64_t file_offset)
{
    if (!task || !task->vm_map) {
        return (long)RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    long _r = vm_task_mmap_file_backing_locked(task, addr_hint, len, prot, flags, fb, file_offset);
    vm_map_unlock(map);
    return _r;
}

static int vm_task_munmap_locked(task_t* task, uint64_t addr, uint64_t len){
    if (!task || !task->vm_map || !task->address_space) {
        return RDNX_E_INVALID;
    }
    return vm_map_remove((vm_map_t*)task->vm_map, addr, len, task->address_space);
}

int vm_task_munmap(task_t* task, uint64_t addr, uint64_t len)
{
    if (!task || !task->vm_map) {
        return RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    int _r = vm_task_munmap_locked(task, addr, len);
    vm_map_unlock(map);
    return _r;
}



static long vm_task_brk_locked(task_t* task, uint64_t new_break)
{
    if (!task || !task->vm_map) {
        return (long)RDNX_E_INVALID;
    }
    if (new_break == 0) {
        return (long)task->vm_brk_end;
    }
    if (task->vm_brk_base == 0) {
        return (long)RDNX_E_INVALID;
    }
    uint64_t new_end = vm_align_up(new_break);
    if (new_end < task->vm_brk_base) {
        return (long)RDNX_E_INVALID;
    }

    vm_map_t* map = (vm_map_t*)task->vm_map;
    if (new_end > task->vm_brk_end) {
        uint64_t len = new_end - task->vm_brk_end;
        if (len > 0) {
            vm_object_t* obj = vm_object_create(VM_OBJECT_ANON, len);
            if (!obj) {
                return (long)RDNX_E_NOMEM;
            }
            int rc = vm_map_add(map, task->vm_brk_end, len,
                                VM_PROT_READ | VM_PROT_WRITE,
                                VM_MAP_F_ANON | VM_MAP_F_PRIVATE | VM_MAP_F_LAZY,
                                obj,
                                0);
            vm_object_unref(obj);
            if (rc != RDNX_OK) {
                return (long)rc;
            }
        }
    } else if (new_end < task->vm_brk_end) {
        uint64_t len = task->vm_brk_end - new_end;
        (void)vm_map_remove(map, new_end, len, task->address_space);
    }

    task->vm_brk_end = new_end;
    return (long)new_end;
}

long vm_task_brk(task_t* task, uint64_t new_break)
{
    if (!task || !task->vm_map) {
        return (long)RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    long _r = vm_task_brk_locked(task, new_break);
    vm_map_unlock(map);
    return _r;
}

static int vm_entry_is_cow_candidate(const vm_map_entry_t* e)
{
    if (!e) {
        return 0;
    }
    if ((e->prot & VM_PROT_WRITE) == 0) {
        return 0;
    }
    if ((e->flags & VM_MAP_F_PRIVATE) == 0) {
        return 0;
    }
    return 1;
}

static int vm_task_fork_clone_locked(task_t* parent, task_t* child, pmap_t child_pmap){
    if (!parent || !child || !parent->vm_map || !parent->address_space || !child_pmap) {
        return RDNX_E_INVALID;
    }
    vm_map_t* pmap = (vm_map_t*)parent->vm_map;
    vm_map_t* cmap = vm_map_create(child_pmap);
    if (!cmap) {
        return RDNX_E_NOMEM;
    }

    vm_map_entry_t* pe;
    TAILQ_FOREACH(pe, &pmap->entries, link) {
        vm_map_entry_t* ce = (vm_map_entry_t*)kmalloc(sizeof(vm_map_entry_t));
        if (!ce) {
            vm_map_destroy(cmap);
            return RDNX_E_NOMEM;
        }
        memset(ce, 0, sizeof(*ce));
        ce->start = pe->start;
        ce->end = pe->end;
        ce->prot = pe->prot;
        ce->flags = pe->flags;
        ce->object = pe->object;
        ce->object_offset = pe->object_offset;
        if (ce->object) {
            vm_object_ref(ce->object);
        }
        /* Родитель сортирован — хвостовая вставка сохраняет порядок. */
        TAILQ_INSERT_TAIL(&cmap->entries, ce, link);
        cmap->entry_count++;

        int cow = vm_entry_is_cow_candidate(pe);
        if (cow) {
            pe->flags |= VM_MAP_F_COW;
            ce->flags |= VM_MAP_F_COW;
        }

        for (uint64_t va = pe->start; va < pe->end; va += VM_PAGE_SIZE) {
            uint64_t phys = pmap_extract(parent->address_space, va);
            if (!phys) {
                continue;
            }
            uint32_t eff_prot = pe->prot;
            if (cow) {
                eff_prot &= ~VM_PROT_WRITE;
            }

            if (pmap_enter(child_pmap, va, phys, eff_prot,
                           PMAP_ENTER_USER) != RDNX_OK) {
                vm_map_destroy(cmap);
                return RDNX_E_GENERIC;
            }
            (void)vm_page_hold(phys); /* Child mapping reference. */

            if (cow) {
                /* Take write away from the parent too, or the copy is not on
                 * write. */
                (void)pmap_enter(parent->address_space, va, phys, eff_prot,
                                 PMAP_ENTER_USER);
            }
        }
    }

    child->vm_map = cmap;
    child->vm_brk_base = parent->vm_brk_base;
    child->vm_brk_end = parent->vm_brk_end;
    child->vm_mmap_base = parent->vm_mmap_base;
    child->vm_mmap_hint = parent->vm_mmap_hint;
    return RDNX_OK;
}

int vm_task_fork_clone(task_t* parent, task_t* child, pmap_t child_pmap)
{
    if (!parent || !parent->vm_map) {
        return RDNX_E_INVALID;
    }
    /* Замок родителя: его карту мы читаем и помечаем COW. Карта ребёнка
     * строится с нуля и до конца вызова никому не видна. */
    vm_map_t* map = (vm_map_t*)parent->vm_map;
    vm_map_lock(map);
    int _r = vm_task_fork_clone_locked(parent, child, child_pmap);
    vm_map_unlock(map);
    return _r;
}



static void vm_task_destroy_locked(task_t* task){
    if (!task) {
        return;
    }
    if (task->vm_map) {
        vm_map_t* map = (vm_map_t*)task->vm_map;
        vm_map_entry_t* e;
        while ((e = TAILQ_FIRST(&map->entries)) != NULL) {
            (void)vm_map_remove(map,
                                e->start,
                                e->end - e->start,
                                task->address_space);
        }
        vm_map_destroy(map);
        task->vm_map = NULL;
    }
    if (task->address_space) {
        pmap_destroy(task->address_space);
        task->address_space = NULL;
    }
    task->vm_brk_base = 0;
    task->vm_brk_end = 0;
    task->vm_mmap_base = 0;
    task->vm_mmap_hint = 0;
}

void vm_task_destroy(task_t* task)
{
    /* Без замка: сюда приходят с мёртвой задачей — потоков, способных
     * трогать её карту, больше нет, а kmutex нельзя держать через kfree
     * самого себя. */
    vm_task_destroy_locked(task);
}



static int vm_task_msync_locked(task_t* task, uint64_t addr, uint64_t len, uint32_t flags){
    (void)flags;
    if (!task || !task->vm_map || len == 0) {
        return RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t s = vm_align_down(addr);
    uint64_t e = vm_align_up(addr + len);
    if (e <= s) {
        return RDNX_E_INVALID;
    }

    int did = 0;
    vm_map_entry_t* me;
    TAILQ_FOREACH(me, &map->entries, link) {
        if (me->start >= e) {
            break;
        }
        uint64_t rs = (s > me->start) ? s : me->start;
        uint64_t re = (e < me->end) ? e : me->end;
        if (re <= rs) {
            continue;
        }
        if (!me->object || me->object->type != VM_OBJECT_FILE) {
            continue;
        }
        if (me->flags & VM_MAP_F_PRIVATE) {
            continue;
        }
        vm_file_backing_t* fb = (vm_file_backing_t*)me->object->pager_private;
        if (!fb || !fb->data || fb->size == 0) {
            continue;
        }
        uint8_t* dst = (uint8_t*)fb->data;
        for (uint64_t va = rs; va < re; va += VM_PAGE_SIZE) {
            uint64_t page_idx = (me->object_offset + (va - me->start)) / VM_PAGE_SIZE;
            uint64_t phys = vm_object_get_resident_page(me->object, page_idx);
            if (!phys) {
                continue;
            }
            uint64_t off = fb->file_offset + me->object_offset + (va - me->start);
            if (off >= fb->size) {
                continue;
            }
            uint64_t avail = fb->size - off;
            uint64_t copy = (avail > VM_PAGE_SIZE) ? VM_PAGE_SIZE : avail;
            memcpy(dst + off, ARCH_PHYS_TO_VIRT(phys), (size_t)copy);
            did = 1;
        }
    }
    return did ? RDNX_OK : RDNX_E_NOTFOUND;
}

int vm_task_msync(task_t* task, uint64_t addr, uint64_t len, uint32_t flags)
{
    if (!task || !task->vm_map) {
        return RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    int _r = vm_task_msync_locked(task, addr, len, flags);
    vm_map_unlock(map);
    return _r;
}



static int vm_task_mprotect_locked(task_t* task, uint64_t addr, uint64_t len, uint32_t prot){
    if (!task || !task->vm_map || !task->address_space || len == 0) {
        return RDNX_E_INVALID;
    }
    if (prot == VM_PROT_NONE) {
        return RDNX_E_INVALID;
    }

    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t s = vm_align_down(addr);
    uint64_t e = vm_align_up(addr + len);
    if (e <= s) {
        return RDNX_E_INVALID;
    }

    int changed = 0;
    vm_map_entry_t* me;
    TAILQ_FOREACH(me, &map->entries, link) {
        if (me->start >= e) {
            break;
        }
        uint64_t rs = (s > me->start) ? s : me->start;
        uint64_t re = (e < me->end) ? e : me->end;
        if (re <= rs) {
            continue;
        }
        if (rs != me->start || re != me->end) {
            return RDNX_E_UNSUPPORTED;
        }

        me->prot = prot;
            for (uint64_t va = rs; va < re; va += VM_PAGE_SIZE) {
            uint64_t phys = pmap_extract(task->address_space, va);
            phys &= ~(VM_PAGE_SIZE - 1u);
            if (!phys) {
                continue;
            }
            (void)pmap_enter(task->address_space, va, phys, prot, PMAP_ENTER_USER);
        }
        changed = 1;
    }

    return changed ? RDNX_OK : RDNX_E_NOTFOUND;
}

int vm_task_mprotect(task_t* task, uint64_t addr, uint64_t len, uint32_t prot)
{
    if (!task || !task->vm_map) {
        return RDNX_E_INVALID;
    }
    vm_map_t* map = (vm_map_t*)task->vm_map;
    vm_map_lock(map);
    int _r = vm_task_mprotect_locked(task, addr, len, prot);
    vm_map_unlock(map);
    return _r;
}


