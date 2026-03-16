#include "../unix_layer.h"
#include "../../arch/config.h"
#include "../../../mm/vm_map.h"
#include "../../../include/error.h"
#include "../../../include/common.h"

#define UNIX_USER_MIN_VA 0x1000ULL

bool unix_user_range_ok(const void* ptr, size_t len)
{
    if (!ptr) {
        return false;
    }
    uintptr_t start = (uintptr_t)ptr;
    if (start < UNIX_USER_MIN_VA ||
        start > ARCH_USER_CANON_MAX ||
        start >= ARCH_KERNEL_VIRT_BASE) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    uintptr_t end = 0;
    if (__builtin_add_overflow(start, len - 1, &end)) {
        return false;
    }
    return end <= ARCH_USER_CANON_MAX && end < ARCH_KERNEL_VIRT_BASE;
}

static bool unix_user_range_mapped(const void* ptr, size_t len, bool need_write)
{
    if (!ptr) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    task_t* task = task_get_current();
    if (!task) {
        return false;
    }

    vm_map_t* map = (vm_map_t*)task->vm_map;
    if (!map) {
        return false;
    }

    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = 0;
    if (__builtin_add_overflow(start, len - 1, &end)) {
        return false;
    }

    uintptr_t cur = start;
    while (cur <= end) {
        vm_map_entry_t* e = vm_map_lookup(map, cur);
        if (!e) {
            return false;
        }
        if ((e->prot & VM_PROT_READ) == 0) {
            return false;
        }
        if (need_write && (e->prot & VM_PROT_WRITE) == 0) {
            return false;
        }
        if (e->end <= cur) {
            return false;
        }
        cur = (uintptr_t)e->end;
    }

    return true;
}

int unix_copy_from_user(void* kdst, const void* usrc, size_t len)
{
    if (!kdst || !usrc || len == 0) {
        return RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(usrc, len)) {
        return RDNX_E_INVALID;
    }
    if (!unix_user_range_mapped(usrc, len, false)) {
        return RDNX_E_INVALID;
    }
    memcpy(kdst, usrc, len);
    return RDNX_OK;
}

int unix_copy_to_user(void* udst, const void* ksrc, size_t len)
{
    if (!udst || !ksrc || len == 0) {
        return RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(udst, len)) {
        return RDNX_E_INVALID;
    }
    if (!unix_user_range_mapped(udst, len, true)) {
        return RDNX_E_INVALID;
    }
    memcpy(udst, ksrc, len);
    return RDNX_OK;
}

int unix_copy_user_cstr(char* dst, size_t dst_size, const char* user_src)
{
    if (!dst || dst_size == 0 || !user_src) {
        return RDNX_E_INVALID;
    }
    uintptr_t base = (uintptr_t)user_src;
    if (base < UNIX_USER_MIN_VA ||
        base > ARCH_USER_CANON_MAX ||
        base >= ARCH_KERNEL_VIRT_BASE) {
        return RDNX_E_INVALID;
    }
    for (size_t i = 0; i < dst_size; i++) {
        uintptr_t cur = base + i;
        if (cur > ARCH_USER_CANON_MAX || cur >= ARCH_KERNEL_VIRT_BASE) {
            return RDNX_E_INVALID;
        }
        if (!unix_user_range_mapped((const void*)cur, 1, false)) {
            return RDNX_E_INVALID;
        }
        char c = user_src[i];
        dst[i] = c;
        if (c == '\0') {
            return RDNX_OK;
        }
    }
    dst[dst_size - 1] = '\0';
    return RDNX_E_INVALID;
}
