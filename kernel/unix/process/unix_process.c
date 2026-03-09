#include "../unix_layer.h"
#include "../../common/bootlog.h"
#include "../../common/scheduler.h"
#include "../../fabric/spin.h"
#include "../../core/interrupts.h"
#include "../../arch/x86_64/interrupt_frame.h"
#include "../../arch/x86_64/paging.h"
#include "../../vm/vm_map.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"

typedef struct unix_timespec_u {
    int64_t tv_sec;
    int64_t tv_nsec;
} unix_timespec_u_t;

typedef struct unix_sigaction_u {
    uint64_t sa_handler;
    uint64_t sa_flags;
    uint64_t sa_restorer;
    uint64_t sa_mask;
} unix_sigaction_u_t;

enum {
    UNIX_SIG_DFL = 0,
    UNIX_SIG_IGN = 1,
    UNIX_SIG_MAX = 31,
    UNIX_SIGKILL = 9
};

enum {
    UNIX_FUTEX_WAIT = 0,
    UNIX_FUTEX_WAKE = 1,
    UNIX_FUTEX_MAX_SLOTS = 64
};

typedef struct unix_futex_slot {
    uintptr_t addr;
    uint32_t waiters;
    uint32_t gen;
} unix_futex_slot_t;

static spinlock_t unix_futex_lock;
static bool unix_futex_lock_inited = false;
static unix_futex_slot_t unix_futex_slots[UNIX_FUTEX_MAX_SLOTS];

static void unix_futex_init_once(void)
{
    if (!unix_futex_lock_inited) {
        spinlock_init(&unix_futex_lock);
        unix_futex_lock_inited = true;
    }
}

static int unix_futex_find_slot(uintptr_t addr)
{
    for (int i = 0; i < UNIX_FUTEX_MAX_SLOTS; i++) {
        if (unix_futex_slots[i].addr == addr) {
            return i;
        }
    }
    return -1;
}

static int unix_futex_alloc_slot(uintptr_t addr)
{
    for (int i = 0; i < UNIX_FUTEX_MAX_SLOTS; i++) {
        if (unix_futex_slots[i].addr == 0) {
            unix_futex_slots[i].addr = addr;
            unix_futex_slots[i].waiters = 0;
            unix_futex_slots[i].gen = 1;
            return i;
        }
    }
    return -1;
}

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

static void unix_signal_save_frame(task_t* task, const interrupt_frame_t* frame)
{
    task->sig_saved.rip = frame->rip;
    task->sig_saved.rsp = frame->rsp;
    task->sig_saved.rflags = frame->rflags;
    task->sig_saved.rax = frame->rax;
    task->sig_saved.rbx = frame->rbx;
    task->sig_saved.rcx = frame->rcx;
    task->sig_saved.rdx = frame->rdx;
    task->sig_saved.rsi = frame->rsi;
    task->sig_saved.rdi = frame->rdi;
    task->sig_saved.rbp = frame->rbp;
    task->sig_saved.r8 = frame->r8;
    task->sig_saved.r9 = frame->r9;
    task->sig_saved.r10 = frame->r10;
    task->sig_saved.r11 = frame->r11;
    task->sig_saved.r12 = frame->r12;
    task->sig_saved.r13 = frame->r13;
    task->sig_saved.r14 = frame->r14;
    task->sig_saved.r15 = frame->r15;
}

static uint64_t unix_signal_restore_frame(task_t* task, interrupt_frame_t* frame)
{
    if (!task || !frame || !task->sig_in_handler) {
        return (uint64_t)RDNX_E_INVALID;
    }

    frame->rip = task->sig_saved.rip;
    frame->rsp = task->sig_saved.rsp;
    frame->rflags = task->sig_saved.rflags;
    frame->rax = task->sig_saved.rax;
    frame->rbx = task->sig_saved.rbx;
    frame->rcx = task->sig_saved.rcx;
    frame->rdx = task->sig_saved.rdx;
    frame->rsi = task->sig_saved.rsi;
    frame->rdi = task->sig_saved.rdi;
    frame->rbp = task->sig_saved.rbp;
    frame->r8 = task->sig_saved.r8;
    frame->r9 = task->sig_saved.r9;
    frame->r10 = task->sig_saved.r10;
    frame->r11 = task->sig_saved.r11;
    frame->r12 = task->sig_saved.r12;
    frame->r13 = task->sig_saved.r13;
    frame->r14 = task->sig_saved.r14;
    frame->r15 = task->sig_saved.r15;

    task->sig_in_handler = 0;
    task->sig_pending = 0;
    return frame->rax;
}

static int unix_signal_may_send(const task_t* sender, const task_t* target)
{
    if (!sender || !target) {
        return 0;
    }
    if (sender == target) {
        return 1;
    }
    if (sender->euid == 0) {
        return 1;
    }
    return sender->uid == target->uid;
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

void unix_proc_signal_checkpoint(void)
{
    task_t* task = task_get_current();
    thread_t* thr = thread_get_current();
    if (!task || !thr) {
        return;
    }
    if (task->sig_pending == 0 || task->sig_pending > UNIX_SIG_MAX || task->sig_in_handler) {
        return;
    }

    uint32_t sig = task->sig_pending;
    uint64_t handler = task->sigaction[sig].handler;
    uint64_t restorer = task->sigaction[sig].restorer;
    interrupt_frame_t* frame = (interrupt_frame_t*)thr->arch_specific;
    if (!frame) {
        return;
    }

    if (handler == UNIX_SIG_IGN) {
        task->sig_pending = 0;
        return;
    }

    if (handler <= UNIX_SIG_DFL || sig == UNIX_SIGKILL) {
        unix_proc_exit(128u + sig);
        return;
    }

    if (restorer == 0) {
        unix_proc_exit(128u + sig);
        return;
    }

    uint64_t new_rsp = frame->rsp - sizeof(uint64_t);
    uint64_t* ret_addr = (uint64_t*)(uintptr_t)new_rsp;
    if (!unix_user_range_ok(ret_addr, sizeof(*ret_addr))) {
        unix_proc_exit(128u + sig);
        return;
    }

    unix_signal_save_frame(task, frame);
    *ret_addr = restorer;
    frame->rsp = new_rsp;
    frame->rip = handler;
    frame->rdi = sig;
    task->sig_in_handler = 1;
    task->sig_pending = 0;
}

uint64_t unix_proc_kill(uint64_t pid, uint64_t signum)
{
    task_t* self = task_get_current();
    int sig = (int)signum;
    if (!self || pid == 0 || sig < 0 || sig > UNIX_SIG_MAX) {
        return (uint64_t)RDNX_E_INVALID;
    }

    task_t* target = task_find_by_id(pid);
    if (!target) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    if (!unix_signal_may_send(self, target)) {
        return (uint64_t)RDNX_E_DENIED;
    }
    if (sig == 0) {
        return (uint64_t)RDNX_OK;
    }

    target->sig_pending = (uint32_t)sig;
    if (target == self) {
        unix_proc_signal_checkpoint();
    }
    return (uint64_t)RDNX_OK;
}

uint64_t unix_proc_sigaction(uint64_t signum, uint64_t user_act_ptr, uint64_t user_oldact_ptr)
{
    task_t* task = task_get_current();
    int sig = (int)signum;
    unix_sigaction_u_t* new_act = (unix_sigaction_u_t*)(uintptr_t)user_act_ptr;
    unix_sigaction_u_t* old_act = (unix_sigaction_u_t*)(uintptr_t)user_oldact_ptr;
    if (!task || sig <= 0 || sig > UNIX_SIG_MAX || sig == UNIX_SIGKILL) {
        return (uint64_t)RDNX_E_INVALID;
    }

    if (old_act) {
        if (!unix_user_range_ok(old_act, sizeof(*old_act))) {
            return (uint64_t)RDNX_E_INVALID;
        }
        old_act->sa_handler = task->sigaction[sig].handler;
        old_act->sa_flags = task->sigaction[sig].flags;
        old_act->sa_restorer = task->sigaction[sig].restorer;
        old_act->sa_mask = task->sigaction[sig].mask;
    }

    if (new_act) {
        if (!unix_user_range_ok(new_act, sizeof(*new_act))) {
            return (uint64_t)RDNX_E_INVALID;
        }
        task->sigaction[sig].handler = new_act->sa_handler;
        task->sigaction[sig].flags = new_act->sa_flags;
        task->sigaction[sig].restorer = new_act->sa_restorer;
        task->sigaction[sig].mask = new_act->sa_mask;
    }

    return (uint64_t)RDNX_OK;
}

uint64_t unix_proc_sigreturn(void)
{
    task_t* task = task_get_current();
    thread_t* thr = thread_get_current();
    interrupt_frame_t* frame = thr ? (interrupt_frame_t*)thr->arch_specific : NULL;
    if (!task || !frame) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return unix_signal_restore_frame(task, frame);
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

uint64_t unix_time_nanosleep(uint64_t user_req_ptr, uint64_t user_rem_ptr)
{
    const unix_timespec_u_t* req = (const unix_timespec_u_t*)(uintptr_t)user_req_ptr;
    unix_timespec_u_t* rem = (unix_timespec_u_t*)(uintptr_t)user_rem_ptr;
    if (!req || !unix_user_range_ok(req, sizeof(*req))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (rem && !unix_user_range_ok(rem, sizeof(*rem))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000LL) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint64_t ms_from_sec = (uint64_t)req->tv_sec * 1000ULL;
    uint64_t ms_from_nsec = (uint64_t)req->tv_nsec / 1000000ULL;
    uint64_t total_ms = ms_from_sec + ms_from_nsec;
    if ((req->tv_nsec % 1000000LL) != 0) {
        total_ms++;
    }

    if (total_ms > 0) {
        scheduler_sleep(total_ms);
    } else {
        scheduler_yield();
    }

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return (uint64_t)RDNX_OK;
}

uint64_t unix_proc_futex(uint64_t user_uaddr_ptr,
                         uint64_t op,
                         uint64_t val,
                         uint64_t user_timeout_ptr,
                         uint64_t user_uaddr2_ptr,
                         uint64_t val3)
{
    (void)user_uaddr2_ptr;
    (void)val3;

    int32_t* uaddr = (int32_t*)(uintptr_t)user_uaddr_ptr;
    if (!uaddr || !unix_user_range_ok(uaddr, sizeof(*uaddr))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    unix_futex_init_once();

    if ((uint32_t)op == UNIX_FUTEX_WAIT) {
        int32_t expected = (int32_t)val;
        if (*uaddr != expected) {
            return (uint64_t)RDNX_E_BUSY;
        }

        int64_t timeout_ms = -1;
        if (user_timeout_ptr != 0) {
            const unix_timespec_u_t* ts = (const unix_timespec_u_t*)(uintptr_t)user_timeout_ptr;
            if (!unix_user_range_ok(ts, sizeof(*ts))) {
                return (uint64_t)RDNX_E_INVALID;
            }
            if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL) {
                return (uint64_t)RDNX_E_INVALID;
            }
            uint64_t ms_from_sec = (uint64_t)ts->tv_sec * 1000ULL;
            uint64_t ms_from_nsec = (uint64_t)ts->tv_nsec / 1000000ULL;
            timeout_ms = (int64_t)(ms_from_sec + ms_from_nsec);
            if ((ts->tv_nsec % 1000000LL) != 0) {
                timeout_ms++;
            }
        }

        uint64_t deadline = 0;
        if (timeout_ms >= 0) {
            uint64_t now = scheduler_get_ticks();
            uint64_t ticks = ((uint64_t)timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1u)) / SCHEDULER_TIME_SLICE_MS;
            deadline = now + ticks;
        }

        uint32_t wait_gen = 0;
        int slot = -1;
        spinlock_lock(&unix_futex_lock);
        slot = unix_futex_find_slot((uintptr_t)uaddr);
        if (slot < 0) {
            slot = unix_futex_alloc_slot((uintptr_t)uaddr);
        }
        if (slot < 0) {
            spinlock_unlock(&unix_futex_lock);
            return (uint64_t)RDNX_E_BUSY;
        }
        unix_futex_slots[slot].waiters++;
        wait_gen = unix_futex_slots[slot].gen;
        spinlock_unlock(&unix_futex_lock);

        uint64_t rc = (uint64_t)RDNX_OK;
        for (;;) {
            if (*uaddr != expected) {
                rc = (uint64_t)RDNX_E_BUSY;
                break;
            }
            spinlock_lock(&unix_futex_lock);
            uint32_t cur_gen = unix_futex_slots[slot].gen;
            spinlock_unlock(&unix_futex_lock);
            if (cur_gen != wait_gen) {
                rc = (uint64_t)RDNX_OK;
                break;
            }
            if (timeout_ms == 0) {
                rc = (uint64_t)RDNX_E_TIMEOUT;
                break;
            }
            if (timeout_ms > 0 && scheduler_get_ticks() >= deadline) {
                rc = (uint64_t)RDNX_E_TIMEOUT;
                break;
            }
            scheduler_yield();
        }

        spinlock_lock(&unix_futex_lock);
        if (slot >= 0 && slot < UNIX_FUTEX_MAX_SLOTS && unix_futex_slots[slot].waiters > 0) {
            unix_futex_slots[slot].waiters--;
            if (unix_futex_slots[slot].waiters == 0) {
                unix_futex_slots[slot].addr = 0;
                unix_futex_slots[slot].gen = 0;
            }
        }
        spinlock_unlock(&unix_futex_lock);
        return rc;
    }

    if ((uint32_t)op == UNIX_FUTEX_WAKE) {
        uint32_t wake_n = (uint32_t)val;
        if (wake_n == 0) {
            return 0;
        }
        uint32_t ready = 0;
        spinlock_lock(&unix_futex_lock);
        int slot = unix_futex_find_slot((uintptr_t)uaddr);
        if (slot >= 0) {
            uint32_t waiters = unix_futex_slots[slot].waiters;
            ready = (wake_n < waiters) ? wake_n : waiters;
            if (waiters > 0) {
                unix_futex_slots[slot].gen++;
            }
        }
        spinlock_unlock(&unix_futex_lock);
        return (uint64_t)ready;
    }

    return (uint64_t)RDNX_E_UNSUPPORTED;
}
