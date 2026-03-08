#include "../unix_layer.h"
#include "../../common/scheduler.h"
#include "../../../include/console.h"
#include "../../../include/error.h"

uint64_t unix_proc_exit(uint64_t status)
{
    task_t* task = task_get_current();
    if (task) {
        task->exit_code = (int32_t)status;
        task->exited = 1;
    }
    thread_t* cur = thread_get_current();
    kprintf("[EXIT] thread %llu exiting\n",
            (unsigned long long)(cur ? cur->thread_id : 0));
    scheduler_exit_current();
    return 0;
}

uint64_t unix_proc_waitpid(uint64_t pid, uint64_t user_status_ptr)
{
    /* CT-004/CT-005/CT-006 contract point for parent-child wait lifecycle. */
    task_t* self = task_get_current();
    if (!self || pid == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int* user_status = (int*)(uintptr_t)user_status_ptr;
    if (user_status && !unix_user_range_ok(user_status, sizeof(int))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    task_t* child = task_find_by_id(pid);
    if (!child) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    if (child->parent_task_id != self->task_id) {
        return (uint64_t)RDNX_E_DENIED;
    }

    scheduler_reap_finished();
    bool child_threads_gone = (child->thread_count == 0);
    bool child_exited = child->exited ||
                        (child->state == TASK_STATE_ZOMBIE) ||
                        (child->state == TASK_STATE_DEAD);
    if (child_threads_gone && child_exited) {
        if (child->waited) {
            return (uint64_t)RDNX_E_NOTFOUND;
        }
        child->waited = 1;
        if (user_status) {
            *user_status = child->exit_code;
        }
        child->state = TASK_STATE_DEAD;
        task_destroy(child);
        return pid;
    }

    return (uint64_t)RDNX_E_BUSY;
}
