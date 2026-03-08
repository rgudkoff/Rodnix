#ifndef _RODNIX_VM_OBJECT_H
#define _RODNIX_VM_OBJECT_H

#include <stdint.h>

typedef enum {
    VM_OBJECT_ANON = 1,
    VM_OBJECT_FILE = 2
} vm_object_type_t;

typedef struct vm_object {
    vm_object_type_t type;
    uint32_t ref_count;
    uint64_t size;
    void* pager_private;
} vm_object_t;

vm_object_t* vm_object_create(vm_object_type_t type, uint64_t size);
void vm_object_ref(vm_object_t* obj);
void vm_object_unref(vm_object_t* obj);

#endif /* _RODNIX_VM_OBJECT_H */

