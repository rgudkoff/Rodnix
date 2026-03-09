#include "posix_syscall.h"
#include "posix_syscall_handlers.h"
#include "../unix/unix_layer.h"
#include "../../include/error.h"

static posix_syscall_fn_t posix_table[POSIX_SYSCALL_MAX];

void posix_syscall_init(void)
{
    for (uint32_t i = 0; i < POSIX_SYSCALL_MAX; i++) {
        posix_table[i] = NULL;
    }
#define POSIX_REGISTER(num, fn) posix_syscall_register((num), (fn))
#include "posix_sysent.inc"
#undef POSIX_REGISTER
}

int posix_syscall_register(uint32_t num, posix_syscall_fn_t fn)
{
    if (num >= POSIX_SYSCALL_MAX || !fn) {
        return RDNX_E_INVALID;
    }
    posix_table[num] = fn;
    return RDNX_OK;
}

uint64_t posix_syscall_dispatch(uint64_t num,
                                uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    if (num >= POSIX_SYSCALL_MAX || !posix_table[num]) {
        return (uint64_t)RDNX_E_UNSUPPORTED;
    }
    uint64_t ret = posix_table[num](a1, a2, a3, a4, a5, a6);
    if (num != POSIX_SYS_SIGRETURN) {
        unix_proc_signal_checkpoint();
    }
    return ret;
}
