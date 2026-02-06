/**
 * @file posix_syscall.h
 * @brief Minimal POSIX syscall numbers and wrappers for RodNIX userland
 */

#ifndef _RODNIX_USERLAND_POSIX_SYSCALL_H
#define _RODNIX_USERLAND_POSIX_SYSCALL_H

#include <stdint.h>
#include "syscall.h"

enum {
    POSIX_SYS_NOSYS = 0,
    POSIX_SYS_GETPID = 1,
    POSIX_SYS_GETUID = 2,
    POSIX_SYS_GETEUID = 3,
    POSIX_SYS_GETGID = 4,
    POSIX_SYS_GETEGID = 5,
    POSIX_SYS_SETUID = 6,
    POSIX_SYS_SETEUID = 7,
    POSIX_SYS_SETGID = 8,
    POSIX_SYS_SETEGID = 9,
    POSIX_SYS_OPEN = 10,
    POSIX_SYS_CLOSE = 11,
    POSIX_SYS_READ = 12,
    POSIX_SYS_WRITE = 13,
    POSIX_SYS_UNAME = 14,
    POSIX_SYS_EXIT = 15,
};

static inline long posix_getpid(void)
{
    return rdnx_syscall0(POSIX_SYS_GETPID);
}

static inline long posix_write(int fd, const void* buf, uint64_t len)
{
    return rdnx_syscall6(POSIX_SYS_WRITE, fd, (long)(uintptr_t)buf, (long)len, 0, 0, 0);
}

static inline long posix_read(int fd, void* buf, uint64_t len)
{
    return rdnx_syscall6(POSIX_SYS_READ, fd, (long)(uintptr_t)buf, (long)len, 0, 0, 0);
}

static inline long posix_open(const char* path, int flags)
{
    return rdnx_syscall6(POSIX_SYS_OPEN, (long)(uintptr_t)path, flags, 0, 0, 0, 0);
}

static inline long posix_close(int fd)
{
    return rdnx_syscall1(POSIX_SYS_CLOSE, fd);
}

static inline long posix_uname(void* u)
{
    return rdnx_syscall1(POSIX_SYS_UNAME, (long)(uintptr_t)u);
}

static inline long posix_exit(int code)
{
    return rdnx_syscall1(POSIX_SYS_EXIT, code);
}

#endif /* _RODNIX_USERLAND_POSIX_SYSCALL_H */
