#include "../unix_layer.h"
#include "../../common/loader.h"
#include "../../common/scheduler.h"
#include "../../common/heap.h"
#include "../../../include/common.h"
#include "../../../include/error.h"

typedef struct {
    char* path;
    int argc;
    char* argv[UNIX_ARG_MAX + 1];
} unix_spawn_args_t;

static int unix_exec_apply_cloexec(void* ctx)
{
    task_t* task = (task_t*)ctx;
    if (!task) {
        return RDNX_E_INVALID;
    }
    unix_apply_cloexec(task);
    return RDNX_OK;
}

static int unix_copy_user_strv(const char* const* user_vec,
                               int max_count,
                               int fallback_path_if_empty,
                               const char* fallback_path,
                               const char* local_vec[],
                               char local_buf[][UNIX_PATH_MAX],
                               int* out_count)
{
    if (!local_vec || !local_buf || !out_count || max_count <= 0) {
        return RDNX_E_INVALID;
    }

    int count = 0;
    for (int i = 0; i < max_count + 1; i++) {
        local_vec[i] = NULL;
    }

    if (!user_vec || !unix_user_range_ok(user_vec, sizeof(uintptr_t))) {
        if (fallback_path_if_empty && fallback_path) {
            local_vec[0] = fallback_path;
            local_vec[1] = NULL;
            *out_count = 1;
            return RDNX_OK;
        }
        *out_count = 0;
        return RDNX_OK;
    }

    for (int i = 0; i < max_count; i++) {
        const void* slot = (const void*)(uintptr_t)((uintptr_t)user_vec + (uintptr_t)i * sizeof(uintptr_t));
        if (!unix_user_range_ok(slot, sizeof(uintptr_t))) {
            return RDNX_E_INVALID;
        }
        const char* uptr = user_vec[i];
        if (!uptr) {
            break;
        }
        if (unix_copy_user_cstr(local_buf[i], UNIX_PATH_MAX, uptr) != RDNX_OK) {
            return RDNX_E_INVALID;
        }
        local_vec[i] = local_buf[i];
        count++;
    }

    if (count == 0 && fallback_path_if_empty && fallback_path) {
        local_vec[0] = fallback_path;
        local_vec[1] = NULL;
        *out_count = 1;
        return RDNX_OK;
    }

    local_vec[count] = NULL;
    *out_count = count;
    return RDNX_OK;
}

static void unix_spawn_thread(void* arg)
{
    unix_spawn_args_t* sa = (unix_spawn_args_t*)arg;
    char path_buf[UNIX_PATH_MAX];
    path_buf[0] = '\0';
    const char* argv_local[UNIX_ARG_MAX + 1];
    int argc_local = 0;
    for (int i = 0; i < UNIX_ARG_MAX + 1; i++) {
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
        if (argc_local > UNIX_ARG_MAX) {
            argc_local = UNIX_ARG_MAX;
        }
        for (int i = 0; i < argc_local; i++) {
            argv_local[i] = sa->argv[i];
        }
        argv_local[argc_local] = NULL;
    }
    if (sa && sa->path) {
        kfree(sa->path);
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
    task_t* self = task_get_current();
    int ret = loader_execve_ex(path_buf,
                               argc_local,
                               argv_local,
                               NULL,
                               unix_exec_apply_cloexec,
                               self);
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
        unix_proc_close_fds(task);
        task->exit_code = (ret == RDNX_OK) ? 0 : 127;
        task->exited = 1;
        task->state = TASK_STATE_ZOMBIE;
        unix_proc_notify_waiters(task->parent_task_id);
    }
    scheduler_exit_current();
}

uint64_t unix_fs_exec(uint64_t user_path_ptr, uint64_t user_argv_ptr, uint64_t user_envp_ptr)
{
    /* CT-003 target: exec preserves PID while replacing process image. */
    const char* const* user_argv = (const char* const*)(uintptr_t)user_argv_ptr;
    const char* const* user_envp = (const char* const*)(uintptr_t)user_envp_ptr;
    const char* user_path = (const char*)(uintptr_t)user_path_ptr;
    char path_buf[UNIX_PATH_MAX];
    const char* argv_local[UNIX_ARG_MAX + 1];
    char argv_buf[UNIX_ARG_MAX][UNIX_PATH_MAX];
    const char* env_local[UNIX_ENV_MAX + 1];
    char env_buf[UNIX_ENV_MAX][UNIX_PATH_MAX];
    int argc_local = 0;
    int envc_local = 0;
    int rc = unix_resolve_user_path(user_path, path_buf, sizeof(path_buf));
    if (rc != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rc = unix_copy_user_strv(user_argv,
                             UNIX_ARG_MAX,
                             1,
                             path_buf,
                             argv_local,
                             argv_buf,
                             &argc_local);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    rc = unix_copy_user_strv(user_envp,
                             UNIX_ENV_MAX,
                             0,
                             NULL,
                             env_local,
                             env_buf,
                             &envc_local);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    (void)envc_local;

    task_t* self = task_get_current();
    int ret = loader_execve_ex(path_buf,
                               argc_local,
                               argv_local,
                               env_local,
                               unix_exec_apply_cloexec,
                               self);
    return (uint64_t)ret;
}

uint64_t unix_proc_spawn(uint64_t user_path_ptr, uint64_t user_argv_ptr)
{
    /* CT-001: spawn creates a new child process with a distinct PID. */
    task_t* parent = task_get_current();
    if (!parent) {
        return (uint64_t)RDNX_E_INVALID;
    }

    const char* user_path = (const char*)(uintptr_t)user_path_ptr;
    const char* const* user_argv = (const char* const*)(uintptr_t)user_argv_ptr;
    char path_buf[UNIX_PATH_MAX];
    int rc = unix_resolve_user_path(user_path, path_buf, sizeof(path_buf));
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
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd) - 1);
    child->cwd[sizeof(child->cwd) - 1] = '\0';

    if (unix_clone_fds_for_spawn(parent, child) != RDNX_OK) {
        task_destroy(child);
        return (uint64_t)RDNX_E_GENERIC;
    }
    if (!child->fd_table[0] && !child->fd_table[1] && !child->fd_table[2]) {
        (void)unix_bind_stdio_to_console(child);
    }

    if (!child->fd_table[0] || !child->fd_table[1] || !child->fd_table[2]) {
        task_destroy(child);
        return (uint64_t)RDNX_E_GENERIC;
    }

    unix_spawn_args_t* sa = (unix_spawn_args_t*)kmalloc(sizeof(*sa));
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

    if (!user_argv || !unix_user_range_ok(user_argv, sizeof(uintptr_t))) {
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
        int argv_rc = RDNX_OK;
        for (; argc < UNIX_ARG_MAX; argc++) {
            const char* uptr = NULL;
            const void* slot = (const void*)(uintptr_t)((uintptr_t)user_argv + (uintptr_t)argc * sizeof(uintptr_t));
            if (!unix_user_range_ok(slot, sizeof(uintptr_t))) {
                argv_rc = RDNX_E_INVALID;
                break;
            }
            uptr = user_argv[argc];
            if (!uptr) {
                break;
            }
            if (!unix_user_range_ok(uptr, 1)) {
                argv_rc = RDNX_E_INVALID;
                break;
            }
            char tmp[UNIX_PATH_MAX];
            rc = unix_copy_user_cstr(tmp, sizeof(tmp), uptr);
            if (rc != RDNX_OK) {
                argv_rc = rc;
                break;
            }
            size_t alen = strlen(tmp);
            sa->argv[argc] = (char*)kmalloc(alen + 1);
            if (!sa->argv[argc]) {
                argv_rc = RDNX_E_NOMEM;
                break;
            }
            memcpy(sa->argv[argc], tmp, alen + 1);
        }
        if (argv_rc == RDNX_E_NOMEM) {
            for (int i = 0; i < UNIX_ARG_MAX; i++) {
                if (sa->argv[i]) {
                    kfree(sa->argv[i]);
                }
            }
            kfree(sa->path);
            kfree(sa);
            task_destroy(child);
            return (uint64_t)RDNX_E_NOMEM;
        }
        if (argv_rc != RDNX_OK) {
            for (int i = 0; i < UNIX_ARG_MAX; i++) {
                if (sa->argv[i]) {
                    kfree(sa->argv[i]);
                    sa->argv[i] = NULL;
                }
            }
            argc = 0;
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

    thread_t* th = thread_create(child, unix_spawn_thread, sa);
    if (!th) {
        for (int i = 0; i < UNIX_ARG_MAX; i++) {
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
