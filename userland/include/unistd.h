#ifndef _RODNIX_USERLAND_UNISTD_H
#define _RODNIX_USERLAND_UNISTD_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/fcntl.h>
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
    (void)options;
    return (pid_t)posix_waitpid((long)pid, status);
}

static inline void _exit(int status)
{
    (void)posix_exit(status);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

#ifdef __cplusplus
}
#endif

#endif /* _RODNIX_USERLAND_UNISTD_H */
