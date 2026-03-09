/**
 * @file posix_syscall.h
 * @brief Minimal POSIX syscall numbers and wrappers for RodNIX userland
 */

#ifndef _RODNIX_USERLAND_POSIX_SYSCALL_H
#define _RODNIX_USERLAND_POSIX_SYSCALL_H

#include <stdint.h>
#include "syscall.h"
#include "posix_sysnums.h"
#include "scstat.h"
#include "diskinfo.h"
#include "kmodinfo.h"

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
    return rdnx_syscall3(POSIX_SYS_WRITE, fd, (long)(uintptr_t)buf, (long)len);
}

static inline long posix_read(int fd, void* buf, uint64_t len)
{
#if RDNX_STDIN_INT80_READ_WORKAROUND
    if (fd == 0) {
        return posix_read_int80(fd, buf, len);
    }
#endif
    return rdnx_syscall3(POSIX_SYS_READ, fd, (long)(uintptr_t)buf, (long)len);
}

static inline long posix_open(const char* path, int flags)
{
    return rdnx_syscall2(POSIX_SYS_OPEN, (long)(uintptr_t)path, flags);
}

static inline long posix_close(int fd)
{
    return rdnx_syscall1(POSIX_SYS_CLOSE, fd);
}

static inline long posix_pipe(int pipefd[2])
{
    return rdnx_syscall1(POSIX_SYS_PIPE, (long)(uintptr_t)pipefd);
}

static inline long posix_pipe2(int pipefd[2], int flags)
{
    return rdnx_syscall2(POSIX_SYS_PIPE2, (long)(uintptr_t)pipefd, (long)flags);
}

static inline long posix_poll(void* fds, uint64_t nfds, int timeout_ms)
{
    return rdnx_syscall3(POSIX_SYS_POLL, (long)(uintptr_t)fds, (long)nfds, (long)timeout_ms);
}

static inline long posix_select(int nfds, void* readfds, void* writefds, void* exceptfds, void* timeout)
{
    return rdnx_syscall5(POSIX_SYS_SELECT,
                         (long)nfds,
                         (long)(uintptr_t)readfds,
                         (long)(uintptr_t)writefds,
                         (long)(uintptr_t)exceptfds,
                         (long)(uintptr_t)timeout);
}

static inline long posix_dup(int oldfd)
{
    return rdnx_syscall1(POSIX_SYS_DUP, (long)oldfd);
}

static inline long posix_dup2(int oldfd, int newfd)
{
    return rdnx_syscall2(POSIX_SYS_DUP2, (long)oldfd, (long)newfd);
}

static inline long posix_dup3(int oldfd, int newfd, int flags)
{
    return rdnx_syscall3(POSIX_SYS_DUP3, (long)oldfd, (long)newfd, (long)flags);
}

static inline long posix_chdir(const char* path)
{
    return rdnx_syscall1(POSIX_SYS_CHDIR, (long)(uintptr_t)path);
}

static inline long posix_getcwd(char* buf, uint64_t size)
{
    return rdnx_syscall2(POSIX_SYS_GETCWD, (long)(uintptr_t)buf, (long)size);
}

static inline long posix_mkdir(const char* path)
{
    return rdnx_syscall1(POSIX_SYS_MKDIR, (long)(uintptr_t)path);
}

static inline long posix_unlink(const char* path)
{
    return rdnx_syscall1(POSIX_SYS_UNLINK, (long)(uintptr_t)path);
}

static inline long posix_rmdir(const char* path)
{
    return rdnx_syscall1(POSIX_SYS_RMDIR, (long)(uintptr_t)path);
}

static inline long posix_rename(const char* oldpath, const char* newpath)
{
    return rdnx_syscall2(POSIX_SYS_RENAME, (long)(uintptr_t)oldpath, (long)(uintptr_t)newpath);
}

static inline long posix_fcntl(int fd, int cmd, long arg)
{
    return rdnx_syscall3(POSIX_SYS_FCNTL, fd, cmd, arg);
}

static inline long posix_ioctl(int fd, uint64_t request, void* argp)
{
    return rdnx_syscall3(POSIX_SYS_IOCTL, fd, (long)request, (long)(uintptr_t)argp);
}

static inline long posix_stat(const char* path, void* st)
{
    return rdnx_syscall2(POSIX_SYS_STAT, (long)(uintptr_t)path, (long)(uintptr_t)st);
}

static inline long posix_fstat(int fd, void* st)
{
    return rdnx_syscall2(POSIX_SYS_FSTAT, fd, (long)(uintptr_t)st);
}

static inline long posix_lseek(int fd, long off, int whence)
{
    return rdnx_syscall3(POSIX_SYS_LSEEK, fd, off, whence);
}

static inline long posix_truncate(const char* path, uint64_t size)
{
    return rdnx_syscall2(POSIX_SYS_TRUNCATE, (long)(uintptr_t)path, (long)size);
}

static inline long posix_ftruncate(int fd, uint64_t size)
{
    return rdnx_syscall2(POSIX_SYS_FTRUNCATE, (long)fd, (long)size);
}

static inline long posix_uname(void* u)
{
    return rdnx_syscall1(POSIX_SYS_UNAME, (long)(uintptr_t)u);
}

static inline long posix_exit(int code)
{
    return rdnx_syscall1(POSIX_SYS_EXIT, code);
}

static inline long posix_execve(const char* path, const char* const argv[], const char* const envp[])
{
    return rdnx_syscall3(POSIX_SYS_EXEC,
                         (long)(uintptr_t)path,
                         (long)(uintptr_t)argv,
                         (long)(uintptr_t)envp);
}

static inline long posix_exec(const char* path)
{
    return posix_execve(path, (const char* const*)0, (const char* const*)0);
}

static inline long posix_spawn(const char* path, const char* const argv[])
{
    return rdnx_syscall2(POSIX_SYS_SPAWN, (long)(uintptr_t)path, (long)(uintptr_t)argv);
}

static inline long posix_waitpid(long pid, int* status)
{
    return rdnx_syscall2(POSIX_SYS_WAITPID, pid, (long)(uintptr_t)status);
}

static inline long posix_readdir(const char* path, void* entries, uint64_t len)
{
    return rdnx_syscall3(POSIX_SYS_READDIR, (long)(uintptr_t)path, (long)(uintptr_t)entries, (long)len);
}

static inline long posix_netiflist(void* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall3(POSIX_SYS_NETIFLIST,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total);
}

static inline long posix_hwlist(void* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall3(POSIX_SYS_HWLIST,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total);
}

static inline long posix_fabricls(void* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall3(POSIX_SYS_FABRICLS,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total);
}

static inline long posix_fabricevents(void* entries, uint64_t max_entries, uint32_t* out_read, uint32_t* out_dropped)
{
    return rdnx_syscall4(POSIX_SYS_FABRICEVENTS,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_read,
                         (long)(uintptr_t)out_dropped);
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
    return rdnx_syscall2(POSIX_SYS_MUNMAP, (long)(uintptr_t)addr, (long)len);
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

static inline long posix_nanosleep(const void* req, void* rem)
{
    return rdnx_syscall2(POSIX_SYS_NANOSLEEP, (long)(uintptr_t)req, (long)(uintptr_t)rem);
}

static inline long posix_futex(int* uaddr,
                               int op,
                               int val,
                               const void* timeout,
                               int* uaddr2,
                               int val3)
{
    return rdnx_syscall6(POSIX_SYS_FUTEX,
                         (long)(uintptr_t)uaddr,
                         (long)op,
                         (long)val,
                         (long)(uintptr_t)timeout,
                         (long)(uintptr_t)uaddr2,
                         (long)val3);
}

static inline long posix_kill(long pid, int signum)
{
    return rdnx_syscall2(POSIX_SYS_KILL, pid, (long)signum);
}

static inline long posix_sigaction(int signum, const void* act, void* oldact)
{
    return rdnx_syscall3(POSIX_SYS_SIGACTION,
                         (long)signum,
                         (long)(uintptr_t)act,
                         (long)(uintptr_t)oldact);
}

static inline long posix_sigreturn(void)
{
    return rdnx_syscall0(POSIX_SYS_SIGRETURN);
}

static inline long posix_scstat(rodnix_scstat_entry_t* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall3(POSIX_SYS_SCSTAT,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total);
}

static inline long posix_blocklist(rodnix_blockdev_info_t* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall3(POSIX_SYS_BLOCKLIST,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total);
}

static inline long posix_blockread(const char* dev_name, uint64_t lba, void* out, uint64_t out_len)
{
    return rdnx_syscall4(POSIX_SYS_BLOCKREAD,
                         (long)(uintptr_t)dev_name,
                         (long)lba,
                         (long)(uintptr_t)out,
                         (long)out_len);
}

static inline long posix_blockwrite(const char* dev_name, uint64_t lba, const void* in, uint64_t in_len)
{
    return rdnx_syscall4(POSIX_SYS_BLOCKWRITE,
                         (long)(uintptr_t)dev_name,
                         (long)lba,
                         (long)(uintptr_t)in,
                         (long)in_len);
}

static inline long posix_kmodls(rodnix_kmod_info_t* entries, uint64_t max_entries, uint32_t* out_total)
{
    return rdnx_syscall3(POSIX_SYS_KMODLS,
                         (long)(uintptr_t)entries,
                         (long)max_entries,
                         (long)(uintptr_t)out_total);
}

static inline long posix_kmodload(const char* path)
{
    return rdnx_syscall1(POSIX_SYS_KMODLOAD, (long)(uintptr_t)path);
}

static inline long posix_kmodunload(const char* name)
{
    return rdnx_syscall1(POSIX_SYS_KMODUNLOAD, (long)(uintptr_t)name);
}

#endif /* _RODNIX_USERLAND_POSIX_SYSCALL_H */
