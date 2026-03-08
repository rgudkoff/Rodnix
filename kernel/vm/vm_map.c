#include "vm_map.h"
#include "vm_pager.h"
#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/config.h"
#include "../common/heap.h"
#include "../../include/common.h"
#include "../../include/error.h"

#define VM_USER_MIN      0x0000000000001000ULL
#define VM_USER_MAX      0x00007FFFFFFFF000ULL
#define VM_DEFAULT_MMAP  0x0000000060000000ULL

static inline uint64_t vm_align_down(uint64_t v)
{
    return v & ~(VM_PAGE_SIZE - 1u);
}

static inline uint64_t vm_align_up(uint64_t v)
{
    return (v + VM_PAGE_SIZE - 1u) & ~(VM_PAGE_SIZE - 1u);
}

static vm_map_t* vm_map_create(uint64_t pml4_phys)
{
    vm_map_t* map = (vm_map_t*)kmalloc(sizeof(vm_map_t));
    if (!map) {
        return NULL;
    }
    memset(map, 0, sizeof(*map));
    map->pml4_phys = pml4_phys;
    return map;
}

static void vm_map_destroy(vm_map_t* map)
{
    if (!map) {
        return;
    }
    for (uint32_t i = 0; i < map->entry_count; i++) {
        if (map->entries[i].object) {
            vm_object_unref(map->entries[i].object);
            map->entries[i].object = NULL;
        }
    }
    kfree(map);
}

static int vm_range_valid(uint64_t start, uint64_t end)
{
    if (start < VM_USER_MIN || end <= start || end > VM_USER_MAX) {
        return 0;
    }
    if (start >= X86_64_KERNEL_VIRT_BASE || end >= X86_64_KERNEL_VIRT_BASE) {
        return 0;
    }
    return 1;
}

static int vm_map_overlap(vm_map_t* map, uint64_t start, uint64_t end)
{
    for (uint32_t i = 0; i < map->entry_count; i++) {
        vm_map_entry_t* e = &map->entries[i];
        if (end <= e->start || start >= e->end) {
            continue;
        }
        return 1;
    }
    return 0;
}

static int vm_map_add(vm_map_t* map, uint64_t start, uint64_t len, uint32_t prot, uint32_t flags, vm_object_t* obj)
{
    uint64_t s = vm_align_down(start);
    uint64_t e = vm_align_up(start + len);
    if (!map || len == 0 || !vm_range_valid(s, e)) {
        return RDNX_E_INVALID;
    }
    if (map->entry_count >= VM_MAP_MAX_ENTRIES) {
        return RDNX_E_BUSY;
    }
    if (vm_map_overlap(map, s, e)) {
        return RDNX_E_BUSY;
    }

    vm_map_entry_t* ne = &map->entries[map->entry_count++];
    memset(ne, 0, sizeof(*ne));
    ne->start = s;
    ne->end = e;
    ne->prot = prot;
    ne->flags = flags;
    ne->object = obj;
    ne->object_offset = 0;
    if (obj) {
        vm_object_ref(obj);
    }
    return RDNX_OK;
}

static int vm_map_remove(vm_map_t* map, uint64_t start, uint64_t len, uint64_t pml4_phys)
{
    uint64_t s = vm_align_down(start);
    uint64_t e = vm_align_up(start + len);
    if (!map || len == 0 || e <= s) {
        return RDNX_E_INVALID;
    }
    int removed = 0;
    for (uint32_t i = 0; i < map->entry_count;) {
        vm_map_entry_t* cur = &map->entries[i];
        uint64_t rs = (s > cur->start) ? s : cur->start;
        uint64_t re = (e < cur->end) ? e : cur->end;
        if (re <= rs) {
            i++;
            continue;
        }

        if (pml4_phys == map->pml4_phys) {
            for (uint64_t va = rs; va < re; va += VM_PAGE_SIZE) {
                if (paging_get_physical(va) != 0) {
                    (void)paging_unmap_page(va);
                }
            }
        }
        removed = 1;

        if (rs == cur->start && re == cur->end) {
            if (cur->object) {
                vm_object_unref(cur->object);
            }
            if (i + 1 < map->entry_count) {
                memmove(cur, cur + 1, (map->entry_count - i - 1) * sizeof(vm_map_entry_t));
            }
            map->entry_count--;
            continue;
        }

        if (rs == cur->start) {
            cur->start = re;
            cur->object_offset += (re - rs);
            i++;
            continue;
        }

        if (re == cur->end) {
            cur->end = rs;
            i++;
            continue;
        }

        if (map->entry_count >= VM_MAP_MAX_ENTRIES) {
            return RDNX_E_BUSY;
        }
        vm_map_entry_t tail = *cur;
        tail.start = re;
        tail.object_offset += (re - cur->start);
        if (tail.object) {
            vm_object_ref(tail.object);
        }
        cur->end = rs;
        if (i + 1 < map->entry_count) {
            memmove(&map->entries[i + 2], &map->entries[i + 1],
                    (map->entry_count - i - 1) * sizeof(vm_map_entry_t));
        }
        map->entries[i + 1] = tail;
        map->entry_count++;
        i += 2;
    }
    return removed ? RDNX_OK : RDNX_E_NOTFOUND;
}

vm_map_entry_t* vm_map_lookup(vm_map_t* map, uint64_t addr)
{
    if (!map) {
        return NULL;
    }
    for (uint32_t i = 0; i < map->entry_count; i++) {
        vm_map_entry_t* e = &map->entries[i];
        if (addr >= e->start && addr < e->end) {
            return e;
        }
    }
    return NULL;
}

static uint64_t vm_find_gap(vm_map_t* map, uint64_t hint, uint64_t len)
{
    uint64_t s = vm_align_up(hint);
    uint64_t e = vm_align_up(hint + len);
    if (e <= s) {
        return 0;
    }
    if (s < VM_DEFAULT_MMAP) {
        s = VM_DEFAULT_MMAP;
        e = s + vm_align_up(len);
    }

    while (e < VM_USER_MAX) {
        if (!vm_map_overlap(map, s, e)) {
            return s;
        }
        s += VM_PAGE_SIZE;
        e += VM_PAGE_SIZE;
    }
    return 0;
}

int vm_task_prepare_exec(task_t* task, uint64_t user_pml4_phys)
{
    if (!task || !user_pml4_phys) {
        return RDNX_E_INVALID;
    }

    if (task->vm_map) {
        vm_map_destroy(task->vm_map);
        task->vm_map = NULL;
    }

    vm_map_t* map = vm_map_create(user_pml4_phys);
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

int vm_task_map_fixed(task_t* task, uint64_t start, uint64_t len, uint32_t prot, uint32_t flags)
{
    if (!task || !task->vm_map) {
        return RDNX_E_INVALID;
    }
    return vm_map_add((vm_map_t*)task->vm_map, start, len, prot, flags | VM_MAP_F_FIXED, NULL);
}

int vm_task_set_brk_base(task_t* task, uint64_t brk_base)
{
    if (!task) {
        return RDNX_E_INVALID;
    }
    uint64_t b = vm_align_up(brk_base);
    task->vm_brk_base = b;
    task->vm_brk_end = b;
    return RDNX_OK;
}

long vm_task_mmap(task_t* task, uint64_t addr_hint, uint64_t len, uint32_t prot, uint32_t flags)
{
    if (!task || !task->vm_map || len == 0) {
        return (long)RDNX_E_INVALID;
    }
    uint64_t alen = vm_align_up(len);
    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t addr = 0;

    if ((flags & VM_MAP_F_FIXED) != 0) {
        addr = vm_align_down(addr_hint);
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
    int rc = vm_map_add(map, addr, alen, prot, flags | VM_MAP_F_LAZY | VM_MAP_F_ANON, obj);
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
    if (!task || !task->vm_map || len == 0 || !data) {
        return (long)RDNX_E_INVALID;
    }
    uint64_t alen = vm_align_up(len);
    vm_map_t* map = (vm_map_t*)task->vm_map;
    uint64_t addr = 0;

    if ((flags & VM_MAP_F_FIXED) != 0) {
        addr = vm_align_down(addr_hint);
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
    fb->file_offset = file_offset;
    obj->pager_private = fb;

    int rc = vm_map_add(map, addr, alen, prot, flags | VM_MAP_F_LAZY, obj);
    vm_object_unref(obj);
    if (rc != RDNX_OK) {
        return (long)rc;
    }
    task->vm_mmap_hint = addr + alen;
    return (long)addr;
}

int vm_task_munmap(task_t* task, uint64_t addr, uint64_t len)
{
    if (!task || !task->vm_map || !task->address_space) {
        return RDNX_E_INVALID;
    }
    return vm_map_remove((vm_map_t*)task->vm_map, addr, len, (uint64_t)(uintptr_t)task->address_space);
}

long vm_task_brk(task_t* task, uint64_t new_break)
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
                                obj);
            vm_object_unref(obj);
            if (rc != RDNX_OK) {
                return (long)rc;
            }
        }
    } else if (new_end < task->vm_brk_end) {
        uint64_t len = task->vm_brk_end - new_end;
        (void)vm_map_remove(map, new_end, len, (uint64_t)(uintptr_t)task->address_space);
    }

    task->vm_brk_end = new_end;
    return (long)new_end;
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

int vm_task_fork_clone(task_t* parent, task_t* child, uint64_t child_pml4_phys)
{
    if (!parent || !child || !parent->vm_map || !parent->address_space || !child_pml4_phys) {
        return RDNX_E_INVALID;
    }
    vm_map_t* pmap = (vm_map_t*)parent->vm_map;
    vm_map_t* cmap = vm_map_create(child_pml4_phys);
    if (!cmap) {
        return RDNX_E_NOMEM;
    }

    for (uint32_t i = 0; i < pmap->entry_count; i++) {
        if (cmap->entry_count >= VM_MAP_MAX_ENTRIES) {
            vm_map_destroy(cmap);
            return RDNX_E_BUSY;
        }
        vm_map_entry_t pe = pmap->entries[i];
        vm_map_entry_t* ce = &cmap->entries[cmap->entry_count++];
        *ce = pe;
        if (ce->object) {
            vm_object_ref(ce->object);
        }

        int cow = vm_entry_is_cow_candidate(&pe);
        if (cow) {
            pmap->entries[i].flags |= VM_MAP_F_COW;
            ce->flags |= VM_MAP_F_COW;
        }

        for (uint64_t va = pe.start; va < pe.end; va += VM_PAGE_SIZE) {
            uint64_t phys = paging_get_physical(va) & ~(VM_PAGE_SIZE - 1u);
            if (!phys) {
                continue;
            }
            uint32_t eff_prot = pe.prot;
            if (cow) {
                eff_prot &= ~VM_PROT_WRITE;
            }
            uint64_t flags = PTE_PRESENT | PTE_USER;
            if (eff_prot & VM_PROT_WRITE) {
                flags |= PTE_RW;
            }
            if ((eff_prot & VM_PROT_EXEC) == 0) {
                flags |= PTE_NX;
            }

            if (paging_map_page_4kb_pml4(child_pml4_phys, va, phys, flags) != RDNX_OK) {
                vm_map_destroy(cmap);
                return RDNX_E_GENERIC;
            }

            if (cow) {
                (void)paging_map_page_4kb_pml4((uint64_t)(uintptr_t)parent->address_space, va, phys, flags);
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

void vm_task_destroy(task_t* task)
{
    if (!task) {
        return;
    }
    if (task->vm_map) {
        vm_map_destroy((vm_map_t*)task->vm_map);
        task->vm_map = NULL;
    }
    task->vm_brk_base = 0;
    task->vm_brk_end = 0;
    task->vm_mmap_base = 0;
    task->vm_mmap_hint = 0;
}
