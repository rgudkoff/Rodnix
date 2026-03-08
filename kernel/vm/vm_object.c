#include "vm_object.h"
#include "../common/heap.h"
#include "../../include/common.h"

vm_object_t* vm_object_create(vm_object_type_t type, uint64_t size)
{
    vm_object_t* obj = (vm_object_t*)kmalloc(sizeof(vm_object_t));
    if (!obj) {
        return NULL;
    }
    memset(obj, 0, sizeof(*obj));
    obj->type = type;
    obj->size = size;
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
        kfree(obj);
    }
}

