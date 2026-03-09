#ifndef _RODNIX_USERLAND_UNISTD_H
#define _RODNIX_USERLAND_UNISTD_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/wait.h>
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

static inline int pipe(int pipefd[2])
{
    long r = posix_pipe(pipefd);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int dup(int oldfd)
{
    long r = posix_dup(oldfd);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

static inline int dup2(int oldfd, int newfd)
{
    long r = posix_dup2(oldfd, newfd);
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

static inline int truncate(const char* path, off_t length)
{
    long r = posix_truncate(path, (uint64_t)length);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int ftruncate(int fd, off_t length)
{
    long r = posix_ftruncate(fd, (uint64_t)length);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
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

static inline int chdir(const char* path)
{
    long r = posix_chdir(path);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline char* getcwd(char* buf, size_t size)
{
    long r = posix_getcwd(buf, (uint64_t)size);
    if (r < 0) {
        errno = (int)(-r);
        return (char*)0;
    }
    return buf;
}

static inline int mkdir(const char* path, mode_t mode)
{
    (void)mode;
    long r = posix_mkdir(path);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int unlink(const char* path)
{
    long r = posix_unlink(path);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int rmdir(const char* path)
{
    long r = posix_rmdir(path);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int rename(const char* oldpath, const char* newpath)
{
    long r = posix_rename(oldpath, newpath);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int ioctl(int fd, unsigned long request, void* argp)
{
    long r = posix_ioctl(fd, (uint64_t)request, argp);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

static inline int isatty(int fd)
{
    long r = posix_ioctl(fd, RDNX_TTY_IOCTL_ISATTY, (void*)0);
    if (r < 0) {
        errno = (int)(-r);
        return 0;
    }
    return (r != 0) ? 1 : 0;
}

static inline int execve(const char* path, char* const argv[], char* const envp[])
{
    long r = posix_execve(path, (const char* const*)argv, (const char* const*)envp);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

static inline int execv(const char* path, char* const argv[])
{
    return execve(path, argv, (char* const*)0);
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
    enum { SYS_TEST_SLEEP = 62 };
    long wr = posix_waitpid((long)pid, status);
    if ((options & WNOHANG) != 0) {
        if (wr == (long)RDNX_E_BUSY) {
            return 0;
        }
        if (wr < 0) {
            errno = (int)(-wr);
            return (pid_t)-1;
        }
        return (pid_t)wr;
    }

    while (wr == (long)RDNX_E_BUSY) {
        (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);
        wr = posix_waitpid((long)pid, status);
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
