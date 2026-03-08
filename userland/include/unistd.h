#ifndef _RODNIX_USERLAND_UNISTD_H
#define _RODNIX_USERLAND_UNISTD_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "posix_syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

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
    long r = posix_getpid();
    if (r < 0) {
        errno = (int)(-r);
        return (pid_t)-1;
    }
    return (pid_t)r;
}

static inline ssize_t read(int fd, void* buf, size_t len)
{
    long r = posix_read(fd, buf, (uint64_t)len);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (ssize_t)r;
}

static inline ssize_t write(int fd, const void* buf, size_t len)
{
    long r = posix_write(fd, buf, (uint64_t)len);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (ssize_t)r;
}

static inline int close(int fd)
{
    long r = posix_close(fd);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

static inline off_t lseek(int fd, off_t off, int whence)
{
    long r = posix_lseek(fd, (long)off, whence);
    if (r < 0) {
        errno = (int)(-r);
        return (off_t)-1;
    }
    return (off_t)r;
}

static inline int fcntl(int fd, int cmd, int arg)
{
    long r = posix_fcntl(fd, cmd, (long)arg);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

static inline int open(const char* path, int flags)
{
    long r = posix_open(path, rdnx_open_flags_from_posix(flags));
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

static inline int execv(const char* path, char* const argv[])
{
    (void)argv;
    long r = posix_exec(path);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

static inline pid_t spawnv(const char* path, char* const argv[])
{
    long r = posix_spawn(path, (const char* const*)argv);
    if (r < 0) {
        errno = (int)(-r);
        return (pid_t)-1;
    }
    return (pid_t)r;
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
    if (wr < 0) {
        errno = (int)(-wr);
        return (pid_t)-1;
    }
    return (pid_t)wr;
}

static inline pid_t fork(void)
{
    long r = posix_fork();
    if (r < 0) {
        errno = (int)(-r);
        return (pid_t)-1;
    }
    return (pid_t)r;
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
        errno = (int)(-r);
        return MAP_FAILED;
    }
    return (void*)(uintptr_t)r;
}

static inline int munmap(void* addr, size_t len)
{
    long r = posix_munmap(addr, (uint64_t)len);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int brk(void* addr)
{
    long r = posix_brk(addr);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline void* sbrk(intptr_t increment)
{
    long cur = posix_brk((void*)0);
    if (cur < 0) {
        errno = (int)(-cur);
        return (void*)-1;
    }
    long next = posix_brk((void*)(uintptr_t)(cur + increment));
    if (next < 0) {
        errno = (int)(-next);
        return (void*)-1;
    }
    return (void*)(uintptr_t)cur;
}

#ifdef __cplusplus
}
#endif

#endif /* _RODNIX_USERLAND_UNISTD_H */
