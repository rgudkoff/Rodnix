#include "posix_syscall.h"
#include "../core/task.h"
#include "../common/security.h"
#include "../fs/vfs.h"
#include "../common/heap.h"
#include "../../include/error.h"
#include "../../include/console.h"
#include "../../include/version.h"
#include "../../include/utsname.h"
#include "../../include/common.h"
#include "../arch/x86_64/config.h"
#include "../input/input.h"
#include "../common/scheduler.h"
#include "../core/task.h"
#include <stddef.h>

static posix_syscall_fn_t posix_table[POSIX_SYSCALL_MAX];
#define POSIX_PATH_MAX 256

static bool posix_is_user_range(const void* ptr, size_t len)
{
    if (!ptr) {
        return false;
    }
    uintptr_t start = (uintptr_t)ptr;
    if (start == 0 || start >= X86_64_KERNEL_VIRT_BASE) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    uintptr_t end = 0;
    if (__builtin_add_overflow(start, len - 1, &end)) {
        return false;
    }
    return end < X86_64_KERNEL_VIRT_BASE;
}

static int posix_copy_user_cstr(char* dst, size_t dst_size, const char* user_src)
{
    if (!dst || dst_size == 0 || !user_src) {
        return RDNX_E_INVALID;
    }
    uintptr_t base = (uintptr_t)user_src;
    if (base == 0 || base >= X86_64_KERNEL_VIRT_BASE) {
        return RDNX_E_INVALID;
    }
    for (size_t i = 0; i < dst_size; i++) {
        uintptr_t cur = base + i;
        if (cur >= X86_64_KERNEL_VIRT_BASE) {
            return RDNX_E_INVALID;
        }
        char c = user_src[i];
        dst[i] = c;
        if (c == '\0') {
            return RDNX_OK;
        }
    }
    dst[dst_size - 1] = '\0';
    return RDNX_E_INVALID;
}

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
    return (uint64_t)RDNX_E_UNSUPPORTED;
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
        return (uint64_t)RDNX_E_DENIED;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task_set_ids(task, (uint32_t)a1, task->gid, task->euid, task->egid);
    return (uint64_t)RDNX_OK;
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
        return (uint64_t)RDNX_E_DENIED;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task_set_ids(task, task->uid, task->gid, (uint32_t)a1, task->egid);
    return (uint64_t)RDNX_OK;
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
        return (uint64_t)RDNX_E_DENIED;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task_set_ids(task, task->uid, (uint32_t)a1, task->euid, task->egid);
    return (uint64_t)RDNX_OK;
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
        return (uint64_t)RDNX_E_DENIED;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task_set_ids(task, task->uid, task->gid, task->euid, (uint32_t)a1);
    return (uint64_t)RDNX_OK;
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
    char path_buf[POSIX_PATH_MAX];
    if (posix_copy_user_cstr(path_buf, sizeof(path_buf), path) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!file) {
        return (uint64_t)RDNX_E_NOMEM;
    }
    if (vfs_open(path_buf, flags, file) != RDNX_OK) {
        kfree(file);
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    task_t* task = task_get_current();
    if (!task) {
        vfs_close(file);
        kfree(file);
        return (uint64_t)RDNX_E_INVALID;
    }
    int fd = task_fd_alloc(task, file);
    if (fd < 0) {
        vfs_close(file);
        kfree(file);
        return (uint64_t)RDNX_E_BUSY;
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
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)a1);
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_close(file);
    kfree(file);
    task_fd_close(task, (int)a1);
    return (uint64_t)RDNX_OK;
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
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)a1);
    void* buf = (void*)(uintptr_t)a2;
    size_t len = (size_t)a3;
    if (!posix_is_user_range(buf, len)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!file) {
        int fd = (int)a1;
        if (fd == 0) {
            /* Minimal stdin support: block until 1 char available */
            char* out = (char*)buf;
            int c;
            do {
                c = input_read_char();
                if (c == -1) {
                    scheduler_ast_check();
                }
            } while (c == -1);
            out[0] = (char)c;
            return 1;
        }
        return (uint64_t)RDNX_E_INVALID;
    }
    int ret = vfs_read(file, buf, len);
    return (ret < 0) ? (uint64_t)RDNX_E_GENERIC : (uint64_t)ret;
}

static uint64_t posix_exit(uint64_t a1,
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
    thread_t* cur = thread_get_current();
    if (cur && cur->joiner) {
        thread_t* joiner = cur->joiner;
        cur->joiner = NULL;
        kprintf("[EXIT] wake joiner tid=%llu state=%d\n",
                (unsigned long long)joiner->thread_id,
                (int)joiner->state);
        joiner->priority = 220;
        joiner->base_priority = joiner->priority;
        joiner->dyn_priority = joiner->priority;
        joiner->inherited_priority = joiner->priority;
        joiner->has_inherited = 0;
        scheduler_wake(joiner);
    }
    kprintf("[EXIT] thread %llu exiting\n",
            (unsigned long long)(cur ? cur->thread_id : 0));
    scheduler_exit_current();
    return 0;
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
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)a1);
    const void* buf = (const void*)(uintptr_t)a2;
    size_t len = (size_t)a3;
    if (!posix_is_user_range(buf, len)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!file) {
        /* Minimal stdout/stderr support */
        int fd = (int)a1;
        if (fd == 1 || fd == 2) {
            const char* s = (const char*)buf;
            for (size_t i = 0; i < len; i++) {
                kputc(s[i]);
            }
            return (uint64_t)len;
        }
        return (uint64_t)RDNX_E_INVALID;
    }
    int ret = vfs_write(file, buf, len);
    return (ret < 0) ? (uint64_t)RDNX_E_GENERIC : (uint64_t)ret;
}

static uint64_t posix_uname(uint64_t a1,
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
    utsname_t* u = (utsname_t*)(uintptr_t)a1;
    if (!posix_is_user_range(u, sizeof(*u))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    memset(u, 0, sizeof(*u));
    u->hdr = RDNX_ABI_INIT(utsname_t);
    strncpy(u->sysname, RODNIX_SYSNAME, sizeof(u->sysname) - 1);
    strncpy(u->nodename, RODNIX_NODENAME, sizeof(u->nodename) - 1);
    strncpy(u->release, RODNIX_RELEASE, sizeof(u->release) - 1);
    strncpy(u->version, RODNIX_VERSION, sizeof(u->version) - 1);
    strncpy(u->machine, X86_64_MACHINE, sizeof(u->machine) - 1);
    return (uint64_t)RDNX_OK;
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
    posix_syscall_register(POSIX_SYS_UNAME, posix_uname);
    posix_syscall_register(POSIX_SYS_EXIT, posix_exit);
}

int posix_syscall_register(uint32_t num, posix_syscall_fn_t fn)
{
    if (num >= POSIX_SYSCALL_MAX || !fn) {
        return RDNX_E_INVALID;
    }
    posix_table[num] = fn;
    return RDNX_OK;
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
        return (uint64_t)RDNX_E_UNSUPPORTED;
    }
    return posix_table[num](a1, a2, a3, a4, a5, a6);
}
