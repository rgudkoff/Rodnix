#ifndef _RODNIX_VM_OBJECT_H
#define _RODNIX_VM_OBJECT_H

#include <stdint.h>

#define VM_OBJECT_PAGE_SIZE 0x1000ULL

typedef enum {
    VM_OBJECT_ANON = 1,
    VM_OBJECT_FILE = 2
} vm_object_type_t;

typedef struct vm_object {
    vm_object_type_t type;
    uint32_t ref_count;
    uint64_t size;
    uint64_t page_count;
    uint64_t* resident_pages;
    void* pager_private;
} vm_object_t;

typedef struct vm_file_backing {
    const uint8_t* data;
    uint64_t size;
    uint64_t file_offset;
    /* Optional demand-paging callback: fills one VM_OBJECT_PAGE_SIZE page.
     * page_off is the byte offset from the start of the file (page-aligned).
     * Returns RDNX_OK on success. */
    int (*read_page)(void* pager_priv, uint64_t page_off, void* page_buf);
    void* pager_priv;
} vm_file_backing_t;

vm_object_t* vm_object_create(vm_object_type_t type, uint64_t size);
void vm_object_ref(vm_object_t* obj);
void vm_object_unref(vm_object_t* obj);
uint64_t vm_object_get_resident_page(const vm_object_t* obj, uint64_t page_index);
int vm_object_set_resident_page(vm_object_t* obj, uint64_t page_index, uint64_t phys);

#endif /* _RODNIX_VM_OBJECT_H */
