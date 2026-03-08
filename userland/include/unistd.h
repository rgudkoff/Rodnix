#ifndef _RODNIX_USERLAND_UNISTD_H
#define _RODNIX_USERLAND_UNISTD_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include "posix_syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

static inline int rdnx_open_flags_from_posix(int flags)
{
    enum {
        VFS_OPEN_READ   = 1 << 0,
        VFS_OPEN_WRITE  = 1 << 1,
        VFS_OPEN_CREATE = 1 << 2,
        VFS_OPEN_TRUNC  = 1 << 3
    };

    int out = 0;
    int acc = flags & O_ACCMODE;

    if (acc == O_WRONLY || acc == O_RDWR) {
        out |= VFS_OPEN_WRITE;
    }
    if (acc == O_RDONLY || acc == O_RDWR) {
        out |= VFS_OPEN_READ;
    }
    if (flags & O_CREAT) {
        out |= VFS_OPEN_CREATE;
    }
    if (flags & O_TRUNC) {
        out |= VFS_OPEN_TRUNC;
    }
    return out;
}

static inline pid_t getpid(void)
{
    return (pid_t)posix_getpid();
}

static inline ssize_t read(int fd, void* buf, size_t len)
{
    return (ssize_t)posix_read(fd, buf, (uint64_t)len);
}

static inline ssize_t write(int fd, const void* buf, size_t len)
{
    return (ssize_t)posix_write(fd, buf, (uint64_t)len);
}

static inline int close(int fd)
{
    return (int)posix_close(fd);
}

static inline int fcntl(int fd, int cmd, int arg)
{
    return (int)posix_fcntl(fd, cmd, (long)arg);
}

static inline int open(const char* path, int flags)
{
    return (int)posix_open(path, rdnx_open_flags_from_posix(flags));
}

static inline int execv(const char* path, char* const argv[])
{
    (void)argv;
    return (int)posix_exec(path);
}

static inline pid_t spawnv(const char* path, char* const argv[])
{
    return (pid_t)posix_spawn(path, (const char* const*)argv);
}

static inline pid_t waitpid(pid_t pid, int* status, int options)
{
    enum { RDNX_E_BUSY = -5 };
    long wr = (long)RDNX_E_BUSY;
    (void)options;
    while (wr == (long)RDNX_E_BUSY) {
        wr = posix_waitpid((long)pid, status);
        if (wr == (long)RDNX_E_BUSY) {
            (void)rdnx_syscall0(0);
        }
    }
    return (pid_t)wr;
}

static inline pid_t fork(void)
{
    return (pid_t)posix_fork();
}

static inline void _exit(int status)
{
    (void)posix_exit(status);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static inline void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off)
{
    long r = posix_mmap(addr, (uint64_t)len, prot, flags, fd, (uint64_t)off);
    if (r < 0) {
        return (void*)0;
    }
    return (void*)(uintptr_t)r;
}

static inline int munmap(void* addr, size_t len)
{
    return (int)posix_munmap(addr, (uint64_t)len);
}

static inline int brk(void* addr)
{
    long r = posix_brk(addr);
    return (r < 0) ? -1 : 0;
}

static inline void* sbrk(intptr_t increment)
{
    long cur = posix_brk((void*)0);
    if (cur < 0) {
        return (void*)0;
    }
    long next = posix_brk((void*)(uintptr_t)(cur + increment));
    if (next < 0) {
        return (void*)0;
    }
    return (void*)(uintptr_t)cur;
}

#ifdef __cplusplus
}
#endif

#endif /* _RODNIX_USERLAND_UNISTD_H */
