/*
 * syscalltest.c
 * Compare fast syscall path vs int80 compatibility path.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "posix_sysnums.h"

#define FD_STDOUT 1
#define CLOCK_MONOTONIC_ID 4

typedef struct {
    uint32_t abi_version;
    uint32_t size;
} rdnx_abi_header_t;

typedef struct {
    rdnx_abi_header_t hdr;
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[64];
    char machine[32];
} utsname_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} timespec_t;

static long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

static long sc0_fast(long n)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(0), "S"(0), "d"(0)
        : "rcx", "r11", "cc", "memory"
    );
    return ret;
}

static long sc1_fast(long n, long a1)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(0), "d"(0)
        : "rcx", "r11", "cc", "memory"
    );
    return ret;
}

static long sc2_fast(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(0)
        : "rcx", "r11", "cc", "memory"
    );
    return ret;
}

static long sc0_int80(long n)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(0), "S"(0), "d"(0)
        : "memory"
    );
    return ret;
}

static long sc1_int80(long n, long a1)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(0), "d"(0)
        : "memory"
    );
    return ret;
}

static long sc2_int80(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(0)
        : "memory"
    );
    return ret;
}

static int str_eq(const char* a, const char* b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static uint64_t u64_abs_long(long v)
{
    if (v < 0) {
        return (uint64_t)(-(v + 1)) + 1ULL;
    }
    return (uint64_t)v;
}

int main(void)
{
    int failed = 0;
    long pid_fast = sc0_fast(POSIX_SYS_GETPID);
    long pid_int80 = sc0_int80(POSIX_SYS_GETPID);

    (void)write_str("syscalltest: getpid fast=");
    write_u64((uint64_t)(pid_fast < 0 ? 0 : pid_fast));
    (void)write_str(" int80=");
    write_u64((uint64_t)(pid_int80 < 0 ? 0 : pid_int80));
    (void)write_str("\n");
    if (pid_fast <= 0 || pid_int80 <= 0 || pid_fast != pid_int80) {
        failed = 1;
    }

    timespec_t ts_fast = {0};
    timespec_t ts_int80 = {0};
    long rc_fast = sc2_fast(POSIX_SYS_CLOCK_GETTIME, CLOCK_MONOTONIC_ID, (long)(uintptr_t)&ts_fast);
    long rc_int80 = sc2_int80(POSIX_SYS_CLOCK_GETTIME, CLOCK_MONOTONIC_ID, (long)(uintptr_t)&ts_int80);
    (void)write_str("syscalltest: clock_gettime fast_rc=");
    write_u64(u64_abs_long(rc_fast));
    (void)write_str(" int80_rc=");
    write_u64(u64_abs_long(rc_int80));
    (void)write_str("\n");
    if (rc_fast != 0 || rc_int80 != 0) {
        failed = 1;
    }

    utsname_t u_fast = {0};
    utsname_t u_int80 = {0};
    rc_fast = sc1_fast(POSIX_SYS_UNAME, (long)(uintptr_t)&u_fast);
    rc_int80 = sc1_int80(POSIX_SYS_UNAME, (long)(uintptr_t)&u_int80);
    (void)write_str("syscalltest: uname fast_rc=");
    write_u64(u64_abs_long(rc_fast));
    (void)write_str(" int80_rc=");
    write_u64(u64_abs_long(rc_int80));
    (void)write_str("\n");
    if (rc_fast != 0 || rc_int80 != 0 || !str_eq(u_fast.sysname, u_int80.sysname)) {
        failed = 1;
    }

    if (failed) {
        (void)write_str("syscalltest: FAIL\n");
        return 1;
    }
    (void)write_str("syscalltest: PASS\n");
    return 0;
}
