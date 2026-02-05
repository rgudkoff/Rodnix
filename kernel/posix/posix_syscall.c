#include "posix_syscall.h"
#include "../core/task.h"
#include "../common/security.h"
#include "../fs/vfs.h"
#include "../common/heap.h"
#include <stddef.h>

static posix_syscall_fn_t posix_table[POSIX_SYSCALL_MAX];

static uint64_t posix_nosys(uint64_t a1,
                            uint64_t a2,
                            uint64_t a3,
                            uint64_t a4,
                            uint64_t a5,
                            uint64_t a6)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    return (uint64_t)-1;
}

static uint64_t posix_getpid(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    return task ? task->task_id : 0;
}

static uint64_t posix_getuid(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    return task ? task->uid : 0;
}

static uint64_t posix_geteuid(uint64_t a1,
                              uint64_t a2,
                              uint64_t a3,
                              uint64_t a4,
                              uint64_t a5,
                              uint64_t a6)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    return task ? task->euid : 0;
}

static uint64_t posix_getgid(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    return task ? task->gid : 0;
}

static uint64_t posix_getegid(uint64_t a1,
                              uint64_t a2,
                              uint64_t a3,
                              uint64_t a4,
                              uint64_t a5,
                              uint64_t a6)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    return task ? task->egid : 0;
}

static uint64_t posix_setuid(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)-1;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)-1;
    }
    task_set_ids(task, (uint32_t)a1, task->gid, task->euid, task->egid);
    return 0;
}

static uint64_t posix_seteuid(uint64_t a1,
                              uint64_t a2,
                              uint64_t a3,
                              uint64_t a4,
                              uint64_t a5,
                              uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)-1;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)-1;
    }
    task_set_ids(task, task->uid, task->gid, (uint32_t)a1, task->egid);
    return 0;
}

static uint64_t posix_setgid(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)-1;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)-1;
    }
    task_set_ids(task, task->uid, (uint32_t)a1, task->euid, task->egid);
    return 0;
}

static uint64_t posix_setegid(uint64_t a1,
                              uint64_t a2,
                              uint64_t a3,
                              uint64_t a4,
                              uint64_t a5,
                              uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)-1;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)-1;
    }
    task_set_ids(task, task->uid, task->gid, task->euid, (uint32_t)a1);
    return 0;
}

static uint64_t posix_open(uint64_t a1,
                           uint64_t a2,
                           uint64_t a3,
                           uint64_t a4,
                           uint64_t a5,
                           uint64_t a6)
{
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    const char* path = (const char*)(uintptr_t)a1;
    int flags = (int)a2;
    vfs_file_t* file = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!file) {
        return (uint64_t)-1;
    }
    if (vfs_open(path, flags, file) != 0) {
        kfree(file);
        return (uint64_t)-1;
    }
    task_t* task = task_get_current();
    int fd = task_fd_alloc(task, file);
    if (fd < 0) {
        vfs_close(file);
        kfree(file);
        return (uint64_t)-1;
    }
    return (uint64_t)fd;
}

static uint64_t posix_close(uint64_t a1,
                            uint64_t a2,
                            uint64_t a3,
                            uint64_t a4,
                            uint64_t a5,
                            uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)a1);
    if (!file) {
        return (uint64_t)-1;
    }
    vfs_close(file);
    kfree(file);
    task_fd_close(task, (int)a1);
    return 0;
}

static uint64_t posix_read(uint64_t a1,
                           uint64_t a2,
                           uint64_t a3,
                           uint64_t a4,
                           uint64_t a5,
                           uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)a1);
    void* buf = (void*)(uintptr_t)a2;
    size_t len = (size_t)a3;
    if (!file || !buf) {
        return (uint64_t)-1;
    }
    int ret = vfs_read(file, buf, len);
    return (ret < 0) ? (uint64_t)-1 : (uint64_t)ret;
}

static uint64_t posix_write(uint64_t a1,
                            uint64_t a2,
                            uint64_t a3,
                            uint64_t a4,
                            uint64_t a5,
                            uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)a1);
    const void* buf = (const void*)(uintptr_t)a2;
    size_t len = (size_t)a3;
    if (!file || !buf) {
        return (uint64_t)-1;
    }
    int ret = vfs_write(file, buf, len);
    return (ret < 0) ? (uint64_t)-1 : (uint64_t)ret;
}

void posix_syscall_init(void)
{
    for (uint32_t i = 0; i < POSIX_SYSCALL_MAX; i++) {
        posix_table[i] = NULL;
    }
    posix_syscall_register(POSIX_SYS_NOSYS, posix_nosys);
    posix_syscall_register(POSIX_SYS_GETPID, posix_getpid);
    posix_syscall_register(POSIX_SYS_GETUID, posix_getuid);
    posix_syscall_register(POSIX_SYS_GETEUID, posix_geteuid);
    posix_syscall_register(POSIX_SYS_GETGID, posix_getgid);
    posix_syscall_register(POSIX_SYS_GETEGID, posix_getegid);
    posix_syscall_register(POSIX_SYS_SETUID, posix_setuid);
    posix_syscall_register(POSIX_SYS_SETEUID, posix_seteuid);
    posix_syscall_register(POSIX_SYS_SETGID, posix_setgid);
    posix_syscall_register(POSIX_SYS_SETEGID, posix_setegid);
    posix_syscall_register(POSIX_SYS_OPEN, posix_open);
    posix_syscall_register(POSIX_SYS_CLOSE, posix_close);
    posix_syscall_register(POSIX_SYS_READ, posix_read);
    posix_syscall_register(POSIX_SYS_WRITE, posix_write);
}

int posix_syscall_register(uint32_t num, posix_syscall_fn_t fn)
{
    if (num >= POSIX_SYSCALL_MAX || !fn) {
        return -1;
    }
    posix_table[num] = fn;
    return 0;
}

uint64_t posix_syscall_dispatch(uint64_t num,
                                uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    if (num >= POSIX_SYSCALL_MAX || !posix_table[num]) {
        return (uint64_t)-1;
    }
    return posix_table[num](a1, a2, a3, a4, a5, a6);
}
