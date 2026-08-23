#include "vm_fault.h"
#include "vm_map.h"
#include "vm_pager.h"
#include "vm_page.h"
#include "pmap.h"
#include "../kernel/arch/config.h"
#include "../include/console.h"
#include "../include/common.h"
#include "../include/error.h"

static int vm_entry_uses_private_object_cow(const vm_map_entry_t* e)
{
    if (!e) {
        return 0;
    }
    if (!e->object) {
        return 0;
    }
    if ((e->flags & VM_MAP_F_PRIVATE) == 0) {
        return 0;
    }
    return 1;
}

static int vm_fault_handle_locked(task_t* task, uint64_t fault_addr, uint64_t err_code, uint64_t rip){
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

    uint64_t current_phys = pmap_extract(task->address_space, va);

    if (current_phys != 0 && is_write &&
        ((e->flags & VM_MAP_F_COW) || vm_entry_uses_private_object_cow(e))) {
        uint64_t new_phys = vm_pager_alloc_zero_page();
        if (!new_phys) {
            return RDNX_E_NOMEM;
        }
        memcpy(ARCH_PHYS_TO_VIRT(new_phys), ARCH_PHYS_TO_VIRT(current_phys), VM_PAGE_SIZE);
        (void)pmap_enter(task->address_space, va, new_phys, e->prot,
                         PMAP_ENTER_USER);
        (void)vm_page_drop(current_phys); /* Drop this mapping's old COW reference. */
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
                (void)vm_page_hold(phys); /* New mapping reference. */
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
            if (e->object && !(is_write && vm_entry_uses_private_object_cow(e))) {
                (void)vm_object_set_resident_page(e->object, obj_page_idx, phys);
            }
        }
        uint32_t eff_prot = e->prot;
        if (!is_write && vm_entry_uses_private_object_cow(e) && (eff_prot & VM_PROT_WRITE)) {
            /* Private file-backed pages must fault again on first write so the
             * writing task receives its own anonymous copy instead of mutating
             * the shared vm_object resident page. */
            eff_prot &= ~VM_PROT_WRITE;
        }
        int rc = pmap_enter(task->address_space, va, phys, eff_prot,
                            PMAP_ENTER_USER);
        if (rc != RDNX_OK) {
            return rc;
        }
        return RDNX_OK;
    }

    /*
     * A mapping is present and the access is one the entry permits -- the
     * protection checks above already rejected the cases where it is not,
     * and the copy-on-write case is handled higher up. So the fault has
     * already been satisfied by somebody else between the processor taking
     * it and this handler getting to look.
     *
     * That is an ordinary outcome on more than one processor, not an error:
     * two threads touch the same page, both fault, the first installs the
     * mapping and the second arrives to find the work done. Reporting it as
     * a failure kills a process for having been second.
     *
     * The kernel-wide lock makes it more likely rather than less. The second
     * faulter blocks on Giant, so by the time it runs the first has finished
     * -- which is why this only started showing up once application
     * processors began executing threads.
     *
     * Returning success retries the instruction, which now finds its
     * translation.
     */
    return RDNX_OK;
}

int vm_fault_handle(task_t* task, uint64_t fault_addr, uint64_t err_code, uint64_t rip)
{
    vm_layer_lock();
    int _r = vm_fault_handle_locked(task, fault_addr, err_code, rip);
    vm_layer_unlock();
    return _r;
}


