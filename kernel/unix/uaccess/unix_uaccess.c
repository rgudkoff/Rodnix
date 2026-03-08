#include "../unix_layer.h"
#include "../../arch/x86_64/config.h"
#include "../../../include/error.h"

#define UNIX_USER_MIN_VA 0x1000ULL
#define UNIX_USER_CANON_MAX 0x00007FFFFFFFFFFFULL

bool unix_user_range_ok(const void* ptr, size_t len)
{
    if (!ptr) {
        return false;
    }
    uintptr_t start = (uintptr_t)ptr;
    if (start < UNIX_USER_MIN_VA ||
        start > UNIX_USER_CANON_MAX ||
        start >= X86_64_KERNEL_VIRT_BASE) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    uintptr_t end = 0;
    if (__builtin_add_overflow(start, len - 1, &end)) {
        return false;
    }
    return end <= UNIX_USER_CANON_MAX && end < X86_64_KERNEL_VIRT_BASE;
}

int unix_copy_user_cstr(char* dst, size_t dst_size, const char* user_src)
{
    if (!dst || dst_size == 0 || !user_src) {
        return RDNX_E_INVALID;
    }
    uintptr_t base = (uintptr_t)user_src;
    if (base < UNIX_USER_MIN_VA ||
        base > UNIX_USER_CANON_MAX ||
        base >= X86_64_KERNEL_VIRT_BASE) {
        return RDNX_E_INVALID;
    }
    for (size_t i = 0; i < dst_size; i++) {
        uintptr_t cur = base + i;
        if (cur > UNIX_USER_CANON_MAX || cur >= X86_64_KERNEL_VIRT_BASE) {
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
