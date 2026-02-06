#ifndef _RODNIX_POSIX_SYSCALL_H
#define _RODNIX_POSIX_SYSCALL_H

#include <stdint.h>

#define POSIX_SYSCALL_MAX 128

typedef uint64_t (*posix_syscall_fn_t)(uint64_t a1,
                                       uint64_t a2,
                                       uint64_t a3,
                                       uint64_t a4,
                                       uint64_t a5,
                                       uint64_t a6);

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

void posix_syscall_init(void);
int posix_syscall_register(uint32_t num, posix_syscall_fn_t fn);
uint64_t posix_syscall_dispatch(uint64_t num,
                                uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6);

#endif /* _RODNIX_POSIX_SYSCALL_H */
