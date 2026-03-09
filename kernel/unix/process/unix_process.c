#include "../unix_layer.h"
#include "../../common/bootlog.h"
#include "../../common/scheduler.h"
#include "../../core/interrupts.h"
#include "../../arch/x86_64/interrupt_frame.h"
#include "../../arch/x86_64/paging.h"
#include "../../vm/vm_map.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"

static int unix_frame_on_thread_stack(const thread_t* t, const interrupt_frame_t* frame)
{
    if (!t || !t->stack || t->stack_size < sizeof(interrupt_frame_t) || !frame) {
        return 0;
    }
    uintptr_t lo = (uintptr_t)t->stack;
    uintptr_t hi = lo + t->stack_size;
    uintptr_t p = (uintptr_t)frame;
    return (p >= lo) && ((p + sizeof(interrupt_frame_t)) <= hi);
}

void unix_proc_notify_waiters(uint64_t parent_task_id)
{
    (void)parent_task_id;
}

void unix_proc_close_fds(task_t* task)
{
    if (!task) {
        return;
    }
    for (int fd = 0; fd < TASK_MAX_FD; fd++) {
        if (task->fd_table[fd]) {
            unix_fd_release(task, fd);
        }
    }
}

uint64_t unix_proc_exit(uint64_t status)
{
    task_t* task = task_get_current();
    if (task) {
        unix_proc_close_fds(task);
        task->exit_code = (int32_t)status;
        task->exited = 1;
        task->state = TASK_STATE_ZOMBIE;
        unix_proc_notify_waiters(task->parent_task_id);
    }
    thread_t* cur = thread_get_current();
    if (bootlog_is_verbose()) {
        kprintf("[EXIT] thread %llu exiting\n",
                (unsigned long long)(cur ? cur->thread_id : 0));
    }
    scheduler_exit_current();
    return 0;
}

uint64_t unix_proc_waitpid(uint64_t pid, uint64_t user_status_ptr)
{
    /* CT-004/CT-005/CT-006 contract point. */
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

    bool child_exited = child->exited ||
                        (child->state == TASK_STATE_ZOMBIE) ||
                        (child->state == TASK_STATE_DEAD);
    if (!child_exited) {
        return (uint64_t)RDNX_E_BUSY;
    }
    if (child->waited) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }

    child->waited = 1;
    if (user_status) {
        *user_status = child->exit_code;
    }
    bool destroy_now = (child->thread_count == 0);
    if (destroy_now) {
        child->state = TASK_STATE_DEAD;
        task_destroy(child);
    }
    return pid;
}

uint64_t unix_proc_fork(void)
{
    task_t* parent = task_get_current();
    thread_t* self_thread = thread_get_current();
    if (!parent || !self_thread || !parent->address_space) {
        return (uint64_t)RDNX_E_INVALID;
    }
    interrupt_frame_t* frame = (interrupt_frame_t*)self_thread->arch_specific;
    if (!unix_frame_on_thread_stack(self_thread, frame)) {
        frame = (interrupt_frame_t*)(uintptr_t)self_thread->context.stack_pointer;
    }
    if (!unix_frame_on_thread_stack(self_thread, frame)) {
        return (uint64_t)RDNX_E_GENERIC;
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

    uint64_t child_pml4 = paging_create_user_pml4();
    if (!child_pml4) {
        task_destroy(child);
        return (uint64_t)RDNX_E_NOMEM;
    }
    child->address_space = (void*)(uintptr_t)child_pml4;
    if (vm_task_fork_clone(parent, child, child_pml4) != RDNX_OK) {
        task_destroy(child);
        return (uint64_t)RDNX_E_GENERIC;
    }

    thread_t* child_thread = thread_create_user_clone(child, frame);
    if (!child_thread) {
        task_destroy(child);
        return (uint64_t)RDNX_E_NOMEM;
    }
    child_thread->priority = self_thread->priority;
    child_thread->base_priority = self_thread->base_priority;
    child_thread->dyn_priority = self_thread->dyn_priority;
    scheduler_add_thread(child_thread);
    return (uint64_t)child->task_id;
}
