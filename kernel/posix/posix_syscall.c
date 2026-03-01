#include "posix_syscall.h"
#include "../core/task.h"
#include "../common/security.h"
#include "../common/loader.h"
#include "../fs/vfs.h"
#include "../common/heap.h"
#include "../../include/error.h"
#include "../../include/console.h"
#include "../../include/version.h"
#include "../../include/utsname.h"
#include "../../include/common.h"
#include "../arch/x86_64/config.h"
#include "../common/scheduler.h"
#include <stddef.h>

static posix_syscall_fn_t posix_table[POSIX_SYSCALL_MAX];
#define POSIX_PATH_MAX 256
#define POSIX_ARG_MAX 16

typedef struct {
    char* path;
    int argc;
    char* argv[POSIX_ARG_MAX + 1];
} posix_spawn_args_t;

static int posix_bind_fd_to_console(task_t* task, int fd, int open_flags)
{
    if (!task || fd < 0 || fd >= TASK_MAX_FD) {
        return RDNX_E_INVALID;
    }

    if (task->fd_table[fd]) {
        vfs_file_t* old = (vfs_file_t*)task->fd_table[fd];
        vfs_close(old);
        kfree(old);
        task->fd_table[fd] = NULL;
    }

    vfs_file_t* file = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!file) {
        return RDNX_E_NOMEM;
    }
    if (vfs_open("/dev/console", open_flags, file) != RDNX_OK) {
        kfree(file);
        return RDNX_E_NOTFOUND;
    }

    task->fd_table[fd] = file;
    return RDNX_OK;
}

int posix_bind_stdio_to_console(task_t* task)
{
    int ret = posix_bind_fd_to_console(task, 0, VFS_OPEN_READ | VFS_OPEN_WRITE);
    if (ret != RDNX_OK) {
        return ret;
    }
    ret = posix_bind_fd_to_console(task, 1, VFS_OPEN_WRITE);
    if (ret != RDNX_OK) {
        return ret;
    }
    return posix_bind_fd_to_console(task, 2, VFS_OPEN_WRITE);
}

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
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    task_t* task = task_get_current();
    if (task) {
        task->exit_code = (int32_t)a1;
        task->exited = 1;
    }
    thread_t* cur = thread_get_current();
    kprintf("[EXIT] thread %llu exiting\n",
            (unsigned long long)(cur ? cur->thread_id : 0));
    scheduler_exit_current();
    return 0;
}

static void posix_spawn_thread(void* arg)
{
    posix_spawn_args_t* sa = (posix_spawn_args_t*)arg;
    char path_buf[POSIX_PATH_MAX];
    path_buf[0] = '\0';
    const char* argv_local[POSIX_ARG_MAX + 1];
    int argc_local = 0;
    for (int i = 0; i < POSIX_ARG_MAX + 1; i++) {
        argv_local[i] = NULL;
    }
    if (sa && sa->path) {
        strncpy(path_buf, sa->path, sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
    }
    if (sa) {
        argc_local = sa->argc;
        if (argc_local < 0) {
            argc_local = 0;
        }
        if (argc_local > POSIX_ARG_MAX) {
            argc_local = POSIX_ARG_MAX;
        }
        for (int i = 0; i < argc_local; i++) {
            argv_local[i] = sa->argv[i];
        }
        argv_local[argc_local] = NULL;
    }
    if (sa) {
        if (sa->path) {
            kfree(sa->path);
        }
    }
    if (path_buf[0] == '\0') {
        if (sa) {
            for (int i = 0; i < argc_local; i++) {
                if (sa->argv[i]) {
                    kfree(sa->argv[i]);
                }
            }
            kfree(sa);
        }
        scheduler_exit_current();
        return;
    }
    int ret = loader_execve(path_buf, argc_local, argv_local);
    if (sa) {
        for (int i = 0; i < argc_local; i++) {
            if (sa->argv[i]) {
                kfree(sa->argv[i]);
            }
        }
        kfree(sa);
    }
    task_t* task = task_get_current();
    if (task) {
        task->exit_code = (ret == RDNX_OK) ? 0 : 127;
        task->exited = 1;
    }
    scheduler_exit_current();
}

static uint64_t posix_exec(uint64_t a1,
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

    const char* user_path = (const char*)(uintptr_t)a1;
    char path_buf[POSIX_PATH_MAX];
    int rc = posix_copy_user_cstr(path_buf, sizeof(path_buf), user_path);
    if (rc != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int ret = loader_exec(path_buf);
    return (uint64_t)ret;
}

static uint64_t posix_spawn(uint64_t a1,
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

    task_t* parent = task_get_current();
    if (!parent) {
        return (uint64_t)RDNX_E_INVALID;
    }

    const char* user_path = (const char*)(uintptr_t)a1;
    const char* const* user_argv = (const char* const*)(uintptr_t)a2;
    char path_buf[POSIX_PATH_MAX];
    int rc = posix_copy_user_cstr(path_buf, sizeof(path_buf), user_path);
    if (rc != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }

    size_t len = strlen(path_buf);
    if (len == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    while (len > 1 && path_buf[len - 1] == '/') {
        path_buf[len - 1] = '\0';
        len--;
    }

    task_t* child = task_create();
    if (!child) {
        return (uint64_t)RDNX_E_NOMEM;
    }
    child->state = TASK_STATE_READY;
    child->parent_task_id = parent->task_id;
    task_set_ids(child, parent->uid, parent->gid, parent->euid, parent->egid);

    if (posix_bind_stdio_to_console(child) != RDNX_OK) {
        task_destroy(child);
        return (uint64_t)RDNX_E_GENERIC;
    }

    posix_spawn_args_t* sa = (posix_spawn_args_t*)kmalloc(sizeof(*sa));
    if (!sa) {
        task_destroy(child);
        return (uint64_t)RDNX_E_NOMEM;
    }
    memset(sa, 0, sizeof(*sa));
    sa->path = (char*)kmalloc(len + 1);
    if (!sa->path) {
        kfree(sa);
        task_destroy(child);
        return (uint64_t)RDNX_E_NOMEM;
    }
    memcpy(sa->path, path_buf, len + 1);

    if (!user_argv) {
        sa->argc = 1;
        sa->argv[0] = (char*)kmalloc(len + 1);
        if (!sa->argv[0]) {
            kfree(sa->path);
            kfree(sa);
            task_destroy(child);
            return (uint64_t)RDNX_E_NOMEM;
        }
        memcpy(sa->argv[0], path_buf, len + 1);
        sa->argv[1] = NULL;
    } else {
        int argc = 0;
        for (; argc < POSIX_ARG_MAX; argc++) {
            const char* uptr = NULL;
            const void* slot = (const void*)(uintptr_t)((uintptr_t)user_argv + (uintptr_t)argc * sizeof(uintptr_t));
            if (!posix_is_user_range(slot, sizeof(uintptr_t))) {
                rc = RDNX_E_INVALID;
                break;
            }
            uptr = user_argv[argc];
            if (!uptr) {
                break;
            }
            char tmp[POSIX_PATH_MAX];
            rc = posix_copy_user_cstr(tmp, sizeof(tmp), uptr);
            if (rc != RDNX_OK) {
                break;
            }
            size_t alen = strlen(tmp);
            sa->argv[argc] = (char*)kmalloc(alen + 1);
            if (!sa->argv[argc]) {
                rc = RDNX_E_NOMEM;
                break;
            }
            memcpy(sa->argv[argc], tmp, alen + 1);
        }
        if (rc != RDNX_OK) {
            for (int i = 0; i < POSIX_ARG_MAX; i++) {
                if (sa->argv[i]) {
                    kfree(sa->argv[i]);
                }
            }
            kfree(sa->path);
            kfree(sa);
            task_destroy(child);
            return (uint64_t)rc;
        }
        if (argc == 0) {
            sa->argv[0] = (char*)kmalloc(len + 1);
            if (!sa->argv[0]) {
                kfree(sa->path);
                kfree(sa);
                task_destroy(child);
                return (uint64_t)RDNX_E_NOMEM;
            }
            memcpy(sa->argv[0], path_buf, len + 1);
            argc = 1;
        }
        sa->argc = argc;
        sa->argv[argc] = NULL;
    }

    thread_t* th = thread_create(child, posix_spawn_thread, sa);
    if (!th) {
        for (int i = 0; i < POSIX_ARG_MAX; i++) {
            if (sa->argv[i]) {
                kfree(sa->argv[i]);
            }
        }
        kfree(sa->path);
        kfree(sa);
        task_destroy(child);
        return (uint64_t)RDNX_E_NOMEM;
    }
    th->priority = 200;
    scheduler_add_thread(th);
    return (uint64_t)child->task_id;
}

static uint64_t posix_waitpid(uint64_t a1,
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

    task_t* self = task_get_current();
    thread_t* self_thread = thread_get_current();
    if (!self || !self_thread || a1 == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int* user_status = (int*)(uintptr_t)a2;
    if (user_status && !posix_is_user_range(user_status, sizeof(int))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint64_t pid = a1;
    task_t* child = task_find_by_id(pid);
    if (!child) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    if (child->parent_task_id != self->task_id) {
        return (uint64_t)RDNX_E_DENIED;
    }

    for (;;) {
        bool exited = child->exited ||
                      (child->thread_count == 0) ||
                      (child->state == TASK_STATE_ZOMBIE) ||
                      (child->state == TASK_STATE_DEAD);
        if (exited) {
            if (user_status) {
                *user_status = child->exit_code;
            }
            child->waited = 1;
            if (child->thread_count == 0) {
                child->state = TASK_STATE_DEAD;
                task_destroy(child);
            }
            return (uint64_t)pid;
        }

        thread_t* child_thread = child->main_thread;
        if (!child_thread) {
            return (uint64_t)RDNX_E_INVALID;
        }
        if (child_thread->joiner && child_thread->joiner != self_thread) {
            return (uint64_t)RDNX_E_BUSY;
        }
        child_thread->joiner = self_thread;
        scheduler_block();
        __asm__ volatile ("int $32");
    }
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
#define POSIX_REGISTER(num, fn) posix_syscall_register((num), (fn))
#include "posix_sysent.inc"
#undef POSIX_REGISTER
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
