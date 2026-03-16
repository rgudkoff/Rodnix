#include "vm_fault.h"
#include "vm_map.h"
#include "vm_pager.h"
#include "vm_page_ref.h"
#include "../arch/paging.h"
#include "../arch/config.h"
#include "../../include/common.h"
#include "../../include/error.h"

static uint64_t vm_pte_flags_from_prot(uint32_t prot)
{
    uint64_t flags = PTE_PRESENT | PTE_USER;
    if (prot & VM_PROT_WRITE) {
        flags |= PTE_RW;
    }
    if ((prot & VM_PROT_EXEC) == 0) {
        flags |= PTE_NX;
    }
    return flags;
}

int vm_fault_handle(task_t* task, uint64_t fault_addr, uint64_t err_code, uint64_t rip)
{
    (void)rip;
    if (!task || !task->vm_map || !task->address_space) {
        return RDNX_E_NOTFOUND;
    }
    if ((err_code & (1u << 2)) == 0) {
        /* Kernel-mode fault: let trap path handle it as fatal. */
        return RDNX_E_DENIED;
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

    uint64_t current_phys = paging_get_physical(va) & ~(VM_PAGE_SIZE - 1u);

    if (current_phys != 0 && is_write && (e->flags & VM_MAP_F_COW)) {
        uint64_t new_phys = vm_pager_alloc_zero_page();
        if (!new_phys) {
            return RDNX_E_NOMEM;
        }
        memcpy(ARCH_PHYS_TO_VIRT(new_phys), ARCH_PHYS_TO_VIRT(current_phys), VM_PAGE_SIZE);
        (void)paging_map_page_4kb_pml4((uint64_t)(uintptr_t)task->address_space,
                                       va,
                                       new_phys,
                                       vm_pte_flags_from_prot(e->prot));
        (void)vm_page_ref_release(current_phys); /* Drop this mapping's old COW reference. */
        return RDNX_OK;
    }

    if (current_phys == 0) {
        uint64_t phys = 0;
        uint64_t obj_page_idx = 0;
        int has_obj_page = 0;

        if (e->object) {
            obj_page_idx = (e->object_offset + (va - e->start)) / VM_PAGE_SIZE;
            phys = vm_object_get_resident_page(e->object, obj_page_idx);
            if (phys) {
                has_obj_page = 1;
                (void)vm_page_ref_retain(phys); /* New mapping reference. */
            }
        }
        if (!has_obj_page) {
            phys = vm_pager_alloc_zero_page();
            if (!phys) {
                return RDNX_E_NOMEM;
            }
            if (e->object && e->object->type == VM_OBJECT_FILE && e->object->pager_private) {
                vm_file_backing_t* fb = (vm_file_backing_t*)e->object->pager_private;
                uint64_t rel = va - e->start;
                uint64_t off = fb->file_offset + e->object_offset + rel;
                if (fb->read_page) {
                    /* Demand-paging: load one page from the backing store. */
                    (void)fb->read_page(fb->pager_priv, off, ARCH_PHYS_TO_VIRT(phys));
                } else if (fb->data && fb->size > 0 && off < fb->size) {
                    uint64_t avail = fb->size - off;
                    uint64_t copy = (avail > VM_PAGE_SIZE) ? VM_PAGE_SIZE : avail;
                    memcpy(ARCH_PHYS_TO_VIRT(phys), fb->data + off, (size_t)copy);
                }
            }
            if (e->object) {
                (void)vm_object_set_resident_page(e->object, obj_page_idx, phys);
            }
        }
        int rc = paging_map_page_4kb_pml4((uint64_t)(uintptr_t)task->address_space,
                                          va,
                                          phys,
                                          vm_pte_flags_from_prot(e->prot));
        if (rc != RDNX_OK) {
            return rc;
        }
        return RDNX_OK;
    }

    return RDNX_E_DENIED;
}
