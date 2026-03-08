#include "syscall.h"
#include "../posix/posix_syscall.h"
#include "scheduler.h"
#include "../../include/error.h"
#include "../../include/console.h"
#include <stddef.h>

static syscall_fn_t syscall_table[SYSCALL_MAX];
static volatile uint64_t g_syscall_int80_count = 0;
static volatile uint64_t g_syscall_fast_count = 0;
static volatile uint64_t g_syscall_int80_by_num[POSIX_SYS_LAST + 1];
static volatile uint64_t g_syscall_fast_by_num[POSIX_SYS_LAST + 1];
_Static_assert(SYS_WRITE > POSIX_SYS_LAST, "legacy SYS_* ids must not overlap POSIX ids");

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
    scheduler_yield();
    return RDNX_OK;
}

static uint64_t sys_write(uint64_t a1,
                          uint64_t a2,
                          uint64_t a3,
                          uint64_t a4,
                          uint64_t a5,
                          uint64_t a6)
{
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    const char* buf = (const char*)(uintptr_t)a1;
    uint64_t len = a2;
    if (!buf) {
        return RDNX_E_INVALID;
    }
    if (len > 4096) {
        len = 4096;
    }
    for (uint64_t i = 0; i < len; i++) {
        kputc(buf[i]);
    }
    return (uint64_t)len;
}

static uint64_t sys_test_sleep(uint64_t a1,
                               uint64_t a2,
                               uint64_t a3,
                               uint64_t a4,
                               uint64_t a5,
                               uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    uint64_t ms = a1;
    if (ms == 0) {
        ms = 1;
    }
    scheduler_sleep(ms);
    return RDNX_OK;
}

void syscall_init(void)
{
    for (uint32_t i = 0; i < SYSCALL_MAX; i++) {
        syscall_table[i] = NULL;
    }
    for (uint32_t i = 0; i <= POSIX_SYS_LAST; i++) {
        g_syscall_int80_by_num[i] = 0;
        g_syscall_fast_by_num[i] = 0;
    }

    syscall_register(SYS_NOP, sys_nop);
    syscall_register(SYS_TEST_SLEEP, sys_test_sleep);
    syscall_register(SYS_WRITE, sys_write);
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

    /* Keep SYS_NOP compatible for legacy ring3 stubs. */
    if (num == SYS_NOP && syscall_table[SYS_NOP]) {
        return syscall_table[SYS_NOP](a1, a2, a3, a4, a5, a6);
    }

    /* Prefer POSIX namespace to avoid collisions with legacy SYS_* ids. */
    uint64_t posix_ret = posix_syscall_dispatch(num, a1, a2, a3, a4, a5, a6);
    if (posix_ret != (uint64_t)RDNX_E_UNSUPPORTED) {
        return posix_ret;
    }

    if (num < SYSCALL_MAX && syscall_table[num]) {
        return syscall_table[num](a1, a2, a3, a4, a5, a6);
    }

    return (uint64_t)RDNX_E_UNSUPPORTED;
}

void syscall_account_int80(void)
{
    g_syscall_int80_count++;
}

void syscall_account_fast(void)
{
    g_syscall_fast_count++;
}

void syscall_account_entry(uint64_t num, int fast_entry)
{
    if (fast_entry) {
        syscall_account_fast();
        if (num <= POSIX_SYS_LAST) {
            g_syscall_fast_by_num[num]++;
        }
        return;
    }

    syscall_account_int80();
    if (num <= POSIX_SYS_LAST) {
        g_syscall_int80_by_num[num]++;
    }
}

uint64_t syscall_get_int80_count(void)
{
    return g_syscall_int80_count;
}

uint64_t syscall_get_fast_count(void)
{
    return g_syscall_fast_count;
}

uint64_t syscall_get_int80_count_for_num(uint64_t num)
{
    if (num > POSIX_SYS_LAST) {
        return 0;
    }
    return g_syscall_int80_by_num[num];
}

uint64_t syscall_get_fast_count_for_num(uint64_t num)
{
    if (num > POSIX_SYS_LAST) {
        return 0;
    }
    return g_syscall_fast_by_num[num];
}
