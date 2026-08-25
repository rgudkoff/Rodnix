#include "vm_pager.h"
#include "vm_page.h"
#include "../kernel/arch/pmm.h"
#include "../kernel/arch/config.h"
#include "../include/common.h"

uint64_t vm_pager_alloc_zero_page(void)
{
    uint64_t phys = pmm_alloc_page_in_zone(PMM_ZONE_NORMAL);
    if (!phys) {
        return 0;
    }
    void* dst = ARCH_PHYS_TO_VIRT(phys);
    memset(dst, 0, ARCH_PAGE_SIZE_4KB);
    (void)vm_page_hold(phys);
    return phys;
}

void vm_pager_fill_page(vm_object_t* obj, uint64_t obj_off, uint64_t phys)
{
    if (!obj || !phys || obj->type != VM_OBJECT_FILE || !obj->pager_private) {
        return;
    }
    vm_file_backing_t* fb = (vm_file_backing_t*)obj->pager_private;
    uint64_t off = fb->file_offset + obj_off;
    if (fb->read_page) {
        /* Demand-paging: одна страница из хранилища. Может спать. */
        (void)fb->read_page(fb->pager_priv, off, ARCH_PHYS_TO_VIRT(phys));
    } else if (fb->data && fb->size > 0 && off < fb->size) {
        uint64_t avail = fb->size - off;
        uint64_t copy = (avail > VM_OBJECT_PAGE_SIZE) ? VM_OBJECT_PAGE_SIZE : avail;
        memcpy(ARCH_PHYS_TO_VIRT(phys), fb->data + off, (size_t)copy);
    }
}
