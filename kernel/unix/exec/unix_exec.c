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

uint64_t unix_fs_exec(uint64_t user_path_ptr)
{
    /* CT-003 target: exec preserves PID while replacing process image. */
    const char* user_path = (const char*)(uintptr_t)user_path_ptr;
    char path_buf[UNIX_PATH_MAX];
    int rc = unix_copy_user_cstr(path_buf, sizeof(path_buf), user_path);
    if (rc != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    int ret = loader_exec(path_buf);
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
    int rc = unix_copy_user_cstr(path_buf, sizeof(path_buf), user_path);
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

    if (unix_bind_stdio_to_console(child) != RDNX_OK) {
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
