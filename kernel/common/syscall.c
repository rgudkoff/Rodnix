#include "syscall.h"
#include "../posix/posix_syscall.h"
#include "../../include/error.h"
#include <stddef.h>

static syscall_fn_t syscall_table[SYSCALL_MAX];

static uint64_t sys_nop(uint64_t a1,
                        uint64_t a2,
                        uint64_t a3,
                        uint64_t a4,
                        uint64_t a5,
                        uint64_t a6)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    return RDNX_OK;
}

void syscall_init(void)
{
    for (uint32_t i = 0; i < SYSCALL_MAX; i++) {
        syscall_table[i] = NULL;
    }

    syscall_register(SYS_NOP, sys_nop);
    posix_syscall_init();
}

int syscall_register(uint32_t num, syscall_fn_t fn)
{
    if (num >= SYSCALL_MAX || !fn) {
        return RDNX_E_INVALID;
    }

    syscall_table[num] = fn;
    return RDNX_OK;
}

uint64_t syscall_dispatch(uint64_t num,
                          uint64_t a1,
                          uint64_t a2,
                          uint64_t a3,
                          uint64_t a4,
                          uint64_t a5,
                          uint64_t a6)
{
    static int logged = 0;
    if (!logged) {
        extern void kputs(const char* str);
        kputs("[SYSCALL] trap received\n");
        logged = 1;
    }
    if (num < SYSCALL_MAX && syscall_table[num]) {
        return syscall_table[num](a1, a2, a3, a4, a5, a6);
    }

    return posix_syscall_dispatch(num, a1, a2, a3, a4, a5, a6);
}
