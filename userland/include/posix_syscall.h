/**
 * @file posix_syscall.h
 * @brief Minimal POSIX syscall numbers and wrappers for RodNIX userland
 */

#ifndef _RODNIX_USERLAND_POSIX_SYSCALL_H
#define _RODNIX_USERLAND_POSIX_SYSCALL_H

#include <stdint.h>
#include "syscall.h"
#include "posix_sysnums.h"

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

static inline long posix_fcntl(int fd, int cmd, long arg)
{
    return rdnx_syscall6(POSIX_SYS_FCNTL, fd, cmd, arg, 0, 0, 0);
}

static inline long posix_uname(void* u)
{
    return rdnx_syscall1(POSIX_SYS_UNAME, (long)(uintptr_t)u);
}

static inline long posix_exit(int code)
{
    return rdnx_syscall1(POSIX_SYS_EXIT, code);
}

static inline long posix_exec(const char* path)
{
    return rdnx_syscall1(POSIX_SYS_EXEC, (long)(uintptr_t)path);
}

static inline long posix_spawn(const char* path, const char* const argv[])
{
    return rdnx_syscall6(POSIX_SYS_SPAWN, (long)(uintptr_t)path, (long)(uintptr_t)argv, 0, 0, 0, 0);
}

static inline long posix_waitpid(long pid, int* status)
{
    return rdnx_syscall6(POSIX_SYS_WAITPID, pid, (long)(uintptr_t)status, 0, 0, 0, 0);
}

static inline long posix_readdir(const char* path, void* entries, uint64_t len)
{
    return rdnx_syscall6(POSIX_SYS_READDIR, (long)(uintptr_t)path, (long)(uintptr_t)entries, (long)len, 0, 0, 0);
}

static inline long posix_netiflist(void* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall6(POSIX_SYS_NETIFLIST,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total,
                         0, 0, 0);
}

static inline long posix_hwlist(void* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall6(POSIX_SYS_HWLIST,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total,
                         0, 0, 0);
}

static inline long posix_fabricls(void* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall6(POSIX_SYS_FABRICLS,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total,
                         0, 0, 0);
}

static inline long posix_fabricevents(void* entries, uint64_t max_entries, uint32_t* out_read, uint32_t* out_dropped)
{
    return rdnx_syscall6(POSIX_SYS_FABRICEVENTS,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_read,
                         (long)(uintptr_t)out_dropped,
                         0, 0);
}

static inline long posix_mmap(void* addr, uint64_t len, int prot, int flags, int fd, uint64_t off)
{
    return rdnx_syscall6(POSIX_SYS_MMAP,
                         (long)(uintptr_t)addr,
                         (long)len,
                         (long)prot,
                         (long)flags,
                         (long)fd,
                         (long)off);
}

static inline long posix_munmap(void* addr, uint64_t len)
{
    return rdnx_syscall6(POSIX_SYS_MUNMAP,
                         (long)(uintptr_t)addr,
                         (long)len,
                         0, 0, 0, 0);
}

static inline long posix_brk(void* new_break)
{
    return rdnx_syscall1(POSIX_SYS_BRK, (long)(uintptr_t)new_break);
}

static inline long posix_fork(void)
{
    return rdnx_syscall0(POSIX_SYS_FORK);
}

#endif /* _RODNIX_USERLAND_POSIX_SYSCALL_H */
