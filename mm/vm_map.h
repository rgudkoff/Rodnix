#ifndef _RODNIX_VM_MAP_H
#define _RODNIX_VM_MAP_H

#include <stdint.h>
#include "../kernel/core/task.h"
#include "../kernel/core/kmutex.h"
#include "vm_object.h"
#include "pmap.h"
#include <bsd/sys/queue.h>

#define VM_PAGE_SIZE 0x1000ULL

#define VM_PROT_NONE  0u
#define VM_PROT_READ  (1u << 0)
#define VM_PROT_WRITE (1u << 1)
#define VM_PROT_EXEC  (1u << 2)

#define VM_MAP_F_ANON        (1u << 0)
#define VM_MAP_F_PRIVATE     (1u << 1)
#define VM_MAP_F_FIXED       (1u << 2)
#define VM_MAP_F_LAZY        (1u << 3)
#define VM_MAP_F_STACK       (1u << 4)
#define VM_MAP_F_COW         (1u << 5)
#define VM_MAP_F_FRAMEBUFFER (1u << 6)  /* physical MMIO mapping; object_offset = display_idx */

/* Запись карты — узел сортированного списка, а не ячейка массива.
 * Список упорядочен по start и не содержит пересечений; и то и другое —
 * инварианты вставки. */
typedef struct vm_map_entry {
    TAILQ_ENTRY(vm_map_entry) link;
    uint64_t start;
    uint64_t end;
    uint32_t prot;
    uint32_t flags;
    vm_object_t* object;
    uint64_t object_offset;
} vm_map_entry_t;

TAILQ_HEAD(vm_map_entry_head, vm_map_entry);

typedef struct vm_map {
    pmap_t pmap;
    /* Замок на карту, спящий. Путь отказа делает ввод-вывод (пейджер читает
     * страницу из файловой системы), а спинлок под вводом-выводом — это
     * секунды недоступности процессора, которые мы уже мерили. Прецедент
     * законен: обработчик отказа уже спит на Giant. Порядок: Giant ->
     * map->lock -> внутренние спинлоки (объект, pmap, куча). */
    kmutex_t lock;
    uint32_t entry_count;
    struct vm_map_entry_head entries;
} vm_map_t;

int vm_task_prepare_exec(task_t* task, pmap_t user_pmap);
int vm_task_map_fixed(task_t* task, uint64_t start, uint64_t len, uint32_t prot, uint32_t flags);
int vm_task_set_brk_base(task_t* task, uint64_t brk_base);
long vm_task_mmap(task_t* task, uint64_t addr_hint, uint64_t len, uint32_t prot, uint32_t flags);
long vm_task_mmap_object(task_t* task,
                         uint64_t addr_hint,
                         uint64_t len,
                         uint32_t prot,
                         uint32_t flags,
                         vm_object_t* obj,
                         uint64_t object_offset);
long vm_task_mmap_file(task_t* task,
                       uint64_t addr_hint,
                       uint64_t len,
                       uint32_t prot,
                       uint32_t flags,
                       const uint8_t* data,
                       uint64_t data_size,
                       uint64_t file_offset);
/* Takes ownership of fb (freed via vm_object_unref when the mapping is torn down). */
long vm_task_mmap_file_backing(task_t* task,
                               uint64_t addr_hint,
                               uint64_t len,
                               uint32_t prot,
                               uint32_t flags,
                               vm_file_backing_t* fb,
                               uint64_t file_offset);
/* Map physical pages (e.g. framebuffer) into user address space eagerly.
 * display_idx is stored in object_offset for release tracking.
 * Returns the virtual address on success, or a negative error code. */
long vm_task_mmap_phys(task_t* task, uint64_t addr_hint, uint64_t len,
                       uint32_t prot, uint64_t phys_base, uint32_t display_idx);

/* Register a hook called when a VM_MAP_F_FRAMEBUFFER entry is fully removed.
 * The hook receives the display_idx stored at mmap time. */
void vm_set_fb_release_hook(void (*fn)(uint32_t display_idx));
int vm_task_munmap(task_t* task, uint64_t addr, uint64_t len);
int vm_task_msync(task_t* task, uint64_t addr, uint64_t len, uint32_t flags);
int vm_task_mprotect(task_t* task, uint64_t addr, uint64_t len, uint32_t prot);
long vm_task_brk(task_t* task, uint64_t new_break);
int vm_task_fork_clone(task_t* parent, task_t* child, pmap_t child_pmap);
void vm_task_destroy(task_t* task);

vm_map_entry_t* vm_map_lookup(vm_map_t* map, uint64_t addr);

/* Замок карты — для пути отказа, который живёт в своём файле. Глобального
 * замка VM-слоя больше нет: карты разных задач независимы. */
void vm_map_lock(vm_map_t* map);
void vm_map_unlock(vm_map_t* map);

#endif /* _RODNIX_VM_MAP_H */
