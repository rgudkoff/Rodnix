#include "vm_object.h"
#include "vm_page_ref.h"
#include "../common/heap.h"
#include "../arch/config.h"
#include "../../include/common.h"
#include "../../include/error.h"

static uint64_t vm_object_align_up(uint64_t value)
{
    return (value + VM_OBJECT_PAGE_SIZE - 1u) & ~(VM_OBJECT_PAGE_SIZE - 1u);
}

vm_object_t* vm_object_create(vm_object_type_t type, uint64_t size)
{
    vm_object_t* obj = (vm_object_t*)kmalloc(sizeof(vm_object_t));
    if (!obj) {
        return NULL;
    }
    memset(obj, 0, sizeof(*obj));
    obj->type = type;
    obj->size = size;
    uint64_t aligned = vm_object_align_up(size ? size : VM_OBJECT_PAGE_SIZE);
    obj->page_count = aligned / VM_OBJECT_PAGE_SIZE;
    obj->resident_pages = (uint64_t*)kmalloc((size_t)(obj->page_count * sizeof(uint64_t)));
    if (!obj->resident_pages) {
        kfree(obj);
        return NULL;
    }
    memset(obj->resident_pages, 0, (size_t)(obj->page_count * sizeof(uint64_t)));
    obj->ref_count = 1;
    return obj;
}

void vm_object_ref(vm_object_t* obj)
{
    if (!obj) {
        return;
    }
    obj->ref_count++;
}

void vm_object_unref(vm_object_t* obj)
{
    if (!obj) {
        return;
    }
    if (obj->ref_count > 0) {
        obj->ref_count--;
    }
    if (obj->ref_count == 0) {
        vm_file_backing_t* fb = (vm_file_backing_t*)obj->pager_private;
        if (obj->type == VM_OBJECT_FILE && fb && fb->data && fb->size > 0) {
            uint8_t* dst = (uint8_t*)fb->data;
            for (uint64_t i = 0; i < obj->page_count; i++) {
                uint64_t phys = obj->resident_pages ? obj->resident_pages[i] : 0;
                if (!phys) {
                    continue;
                }
                uint64_t off = fb->file_offset + i * VM_OBJECT_PAGE_SIZE;
                if (off >= fb->size) {
                    continue;
                }
                uint64_t avail = fb->size - off;
                uint64_t copy = (avail > VM_OBJECT_PAGE_SIZE) ? VM_OBJECT_PAGE_SIZE : avail;
                memcpy(dst + off, ARCH_PHYS_TO_VIRT(phys), (size_t)copy);
            }
        }
        if (obj->resident_pages) {
            for (uint64_t i = 0; i < obj->page_count; i++) {
                uint64_t phys = obj->resident_pages[i];
                if (phys) {
                    (void)vm_page_ref_release(phys); /* Drop vm_object ownership ref. */
                    obj->resident_pages[i] = 0;
                }
            }
            kfree(obj->resident_pages);
            obj->resident_pages = NULL;
        }
        if (obj->pager_private) {
            kfree(obj->pager_private);
            obj->pager_private = NULL;
        }
        kfree(obj);
    }
}

uint64_t vm_object_get_resident_page(const vm_object_t* obj, uint64_t page_index)
{
    if (!obj || !obj->resident_pages || page_index >= obj->page_count) {
        return 0;
    }
    return obj->resident_pages[page_index];
}

int vm_object_set_resident_page(vm_object_t* obj, uint64_t page_index, uint64_t phys)
{
    if (!obj || !obj->resident_pages || page_index >= obj->page_count || !phys) {
        return RDNX_E_INVALID;
    }
    uint64_t old = obj->resident_pages[page_index];
    if (old == phys) {
        return RDNX_OK;
    }
    if (old) {
        (void)vm_page_ref_release(old);
    }
    (void)vm_page_ref_retain(phys);
    obj->resident_pages[page_index] = phys;
    return RDNX_OK;
}
