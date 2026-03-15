#ifndef _RODNIX_VM_MAP_H
#define _RODNIX_VM_MAP_H

#include <stdint.h>
#include "../core/task.h"
#include "vm_object.h"

#define VM_PAGE_SIZE 0x1000ULL
#define VM_MAP_MAX_ENTRIES 128

#define VM_PROT_NONE  0u
#define VM_PROT_READ  (1u << 0)
#define VM_PROT_WRITE (1u << 1)
#define VM_PROT_EXEC  (1u << 2)

#define VM_MAP_F_ANON    (1u << 0)
#define VM_MAP_F_PRIVATE (1u << 1)
#define VM_MAP_F_FIXED   (1u << 2)
#define VM_MAP_F_LAZY    (1u << 3)
#define VM_MAP_F_STACK   (1u << 4)
#define VM_MAP_F_COW     (1u << 5)

typedef struct vm_map_entry {
    uint64_t start;
    uint64_t end;
    uint32_t prot;
    uint32_t flags;
    vm_object_t* object;
    uint64_t object_offset;
} vm_map_entry_t;

typedef struct vm_map {
    uint64_t pml4_phys;
    uint32_t entry_count;
    vm_map_entry_t entries[VM_MAP_MAX_ENTRIES];
} vm_map_t;

int vm_task_prepare_exec(task_t* task, uint64_t user_pml4_phys);
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
int vm_task_munmap(task_t* task, uint64_t addr, uint64_t len);
int vm_task_msync(task_t* task, uint64_t addr, uint64_t len, uint32_t flags);
int vm_task_mprotect(task_t* task, uint64_t addr, uint64_t len, uint32_t prot);
long vm_task_brk(task_t* task, uint64_t new_break);
int vm_task_fork_clone(task_t* parent, task_t* child, uint64_t child_pml4_phys);
void vm_task_destroy(task_t* task);

vm_map_entry_t* vm_map_lookup(vm_map_t* map, uint64_t addr);

#endif /* _RODNIX_VM_MAP_H */
