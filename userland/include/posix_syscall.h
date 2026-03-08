/**
 * @file posix_syscall.h
 * @brief Minimal POSIX syscall numbers and wrappers for RodNIX userland
 */

#ifndef _RODNIX_USERLAND_POSIX_SYSCALL_H
#define _RODNIX_USERLAND_POSIX_SYSCALL_H

#include <stdint.h>
#include "syscall.h"
#include "posix_sysnums.h"

#ifndef RDNX_STDIN_INT80_READ_WORKAROUND
#define RDNX_STDIN_INT80_READ_WORKAROUND 1
#endif

static inline long posix_read_int80(int fd, void* buf, uint64_t len)
{
    long ret;
    register long r10 __asm__("r10") = 0;
    register long r8  __asm__("r8")  = 0;
    register long r9  __asm__("r9")  = 0;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(POSIX_SYS_READ),
          "D"((long)fd),
          "S"((long)(uintptr_t)buf),
          "d"((long)len),
          "r"(r10),
          "r"(r8),
          "r"(r9)
        : "memory"
    );
    return ret;
}

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
#if RDNX_STDIN_INT80_READ_WORKAROUND
    if (fd == 0) {
        return posix_read_int80(fd, buf, len);
    }
#endif
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

static inline long posix_stat(const char* path, void* st)
{
    return rdnx_syscall6(POSIX_SYS_STAT, (long)(uintptr_t)path, (long)(uintptr_t)st, 0, 0, 0, 0);
}

static inline long posix_fstat(int fd, void* st)
{
    return rdnx_syscall6(POSIX_SYS_FSTAT, fd, (long)(uintptr_t)st, 0, 0, 0, 0);
}

static inline long posix_lseek(int fd, long off, int whence)
{
    return rdnx_syscall6(POSIX_SYS_LSEEK, fd, off, whence, 0, 0, 0);
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

static inline long posix_sysinfo(void* out_info)
{
    return rdnx_syscall1(POSIX_SYS_SYSINFO, (long)(uintptr_t)out_info);
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

static inline long posix_clock_gettime(int clock_id, void* tp)
{
    return rdnx_syscall2(POSIX_SYS_CLOCK_GETTIME, (long)clock_id, (long)(uintptr_t)tp);
}

#endif /* _RODNIX_USERLAND_POSIX_SYSCALL_H */
