#include "../unix_layer.h"
#include "../../common/bootlog.h"
#include "../../common/scheduler.h"
#include "../../common/waitq.h"
#include "../../core/interrupts.h"
#include "../../arch/x86_64/interrupt_frame.h"
#include "../../arch/x86_64/paging.h"
#include "../../vm/vm_map.h"
#include "../../../include/console.h"
#include "../../../include/error.h"

static waitq_t unix_child_waitq;
static int unix_child_waitq_inited = 0;

static inline void unix_child_waitq_init_once(void)
{
    if (!unix_child_waitq_inited) {
        waitq_init(&unix_child_waitq, "unix_child_exit");
        unix_child_waitq_inited = 1;
    }
}

static inline irql_t unix_wait_lock(void)
{
    return set_irql(IRQL_HIGH);
}

static inline void unix_wait_unlock(irql_t old)
{
    (void)set_irql(old);
}

void unix_proc_notify_waiters(uint64_t parent_task_id)
{
    (void)parent_task_id;
    unix_child_waitq_init_once();
    irql_t old = unix_wait_lock();
    (void)waitq_wake_all(&unix_child_waitq);
    unix_wait_unlock(old);
}

uint64_t unix_proc_exit(uint64_t status)
{
    task_t* task = task_get_current();
    if (task) {
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
    /*
     * CT-004/CT-005/CT-006 contract point.
     * FreeBSD/XNU-style pattern:
     *   check under lock -> sleep on child-exit wait channel -> recheck
     */
    task_t* self = task_get_current();
    if (!self || pid == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    thread_t* self_thread = thread_get_current();
    if (!self_thread) {
        return (uint64_t)RDNX_E_INVALID;
    }
    unix_child_waitq_init_once();

    int* user_status = (int*)(uintptr_t)user_status_ptr;
    if (user_status && !unix_user_range_ok(user_status, sizeof(int))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    for (;;) {
        irql_t old = unix_wait_lock();
        task_t* child = task_find_by_id(pid);
        if (!child) {
            unix_wait_unlock(old);
            return (uint64_t)RDNX_E_NOTFOUND;
        }
        if (child->parent_task_id != self->task_id) {
            unix_wait_unlock(old);
            return (uint64_t)RDNX_E_DENIED;
        }

        bool child_exited = child->exited ||
                            (child->state == TASK_STATE_ZOMBIE) ||
                            (child->state == TASK_STATE_DEAD);
        if (child_exited) {
            if (child->waited) {
                unix_wait_unlock(old);
                return (uint64_t)RDNX_E_NOTFOUND;
            }

            child->waited = 1;
            if (user_status) {
                *user_status = child->exit_code;
            }
            bool destroy_now = (child->thread_count == 0);
            if (destroy_now) {
                child->state = TASK_STATE_DEAD;
            }
            unix_wait_unlock(old);
            if (destroy_now) {
                task_destroy(child);
            }
            return pid;
        }

        if (!waitq_contains(&unix_child_waitq, self_thread)) {
            int qret = waitq_enqueue(&unix_child_waitq, self_thread);
            if (qret != RDNX_OK && qret != RDNX_E_BUSY) {
                unix_wait_unlock(old);
                return (uint64_t)qret;
            }
        }
        unix_wait_unlock(old);

        (void)waitq_wait(&unix_child_waitq, 0);
    }
}

uint64_t unix_proc_fork(void)
{
    task_t* parent = task_get_current();
    thread_t* self_thread = thread_get_current();
    if (!parent || !self_thread || !parent->address_space) {
        return (uint64_t)RDNX_E_INVALID;
    }
    interrupt_frame_t* frame = (interrupt_frame_t*)self_thread->arch_specific;
    if (!frame) {
        return (uint64_t)RDNX_E_INVALID;
    }

    task_t* child = task_create();
    if (!child) {
        return (uint64_t)RDNX_E_NOMEM;
    }
    child->state = TASK_STATE_READY;
    child->parent_task_id = parent->task_id;
    task_set_ids(child, parent->uid, parent->gid, parent->euid, parent->egid);

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
