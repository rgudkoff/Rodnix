#ifndef _RODNIX_POSIX_SYSCALL_H
#define _RODNIX_POSIX_SYSCALL_H

#include <stdint.h>
#include "../core/task.h"
#include "posix_sysnums.h"

#define POSIX_SYSCALL_MAX 128

typedef uint64_t (*posix_syscall_fn_t)(uint64_t a1,
                                       uint64_t a2,
                                       uint64_t a3,
                                       uint64_t a4,
                                       uint64_t a5,
                                       uint64_t a6);

void posix_syscall_init(void);
int posix_syscall_register(uint32_t num, posix_syscall_fn_t fn);
int posix_bind_stdio_to_console(task_t* task);
uint64_t posix_syscall_dispatch(uint64_t num,
                                uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6);

#endif /* _RODNIX_POSIX_SYSCALL_H */
