#include "../unix_layer.h"
#include "../../../trace/bootlog.h"
#include "../../../sched/internal.h"
#include "../../../sched/scheduler.h"
#include "../../../sched/waitq.h"
#include "../../fabric/spin.h"
#include "../../core/interrupts.h"
#include "../../arch/interrupt_frame.h"
#include "../../arch/paging.h"
#include "../../../mm/vm_map.h"
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
    UNIX_SIGKILL = 9,
    UNIX_SIGTERM = 15,
    UNIX_SIGCHLD  = 17
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
    proc_t* proc = task_proc(task);
    proc->sig_saved.rip = frame->rip;
    proc->sig_saved.rsp = frame->rsp;
    proc->sig_saved.rflags = frame->rflags;
    proc->sig_saved.rax = frame->rax;
    proc->sig_saved.rbx = frame->rbx;
    proc->sig_saved.rcx = frame->rcx;
    proc->sig_saved.rdx = frame->rdx;
    proc->sig_saved.rsi = frame->rsi;
    proc->sig_saved.rdi = frame->rdi;
    proc->sig_saved.rbp = frame->rbp;
    proc->sig_saved.r8 = frame->r8;
    proc->sig_saved.r9 = frame->r9;
    proc->sig_saved.r10 = frame->r10;
    proc->sig_saved.r11 = frame->r11;
    proc->sig_saved.r12 = frame->r12;
    proc->sig_saved.r13 = frame->r13;
    proc->sig_saved.r14 = frame->r14;
    proc->sig_saved.r15 = frame->r15;
}

static uint64_t unix_signal_restore_frame(task_t* task, interrupt_frame_t* frame)
{
    proc_t* proc = task_proc(task);
    if (!task || !frame || !proc->sig_in_handler) {
        return (uint64_t)RDNX_E_INVALID;
    }

    frame->rip = proc->sig_saved.rip;
    frame->rsp = proc->sig_saved.rsp;
    frame->rflags = proc->sig_saved.rflags;
    frame->rax = proc->sig_saved.rax;
    frame->rbx = proc->sig_saved.rbx;
    frame->rcx = proc->sig_saved.rcx;
    frame->rdx = proc->sig_saved.rdx;
    frame->rsi = proc->sig_saved.rsi;
    frame->rdi = proc->sig_saved.rdi;
    frame->rbp = proc->sig_saved.rbp;
    frame->r8 = proc->sig_saved.r8;
    frame->r9 = proc->sig_saved.r9;
    frame->r10 = proc->sig_saved.r10;
    frame->r11 = proc->sig_saved.r11;
    frame->r12 = proc->sig_saved.r12;
    frame->r13 = proc->sig_saved.r13;
    frame->r14 = proc->sig_saved.r14;
    frame->r15 = proc->sig_saved.r15;

    proc->sig_in_handler = 0;
    /* Do not clear sig_pending: other signals may have accumulated during the
     * handler and will be delivered on the next signal checkpoint. */
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
    if (task_proc(sender)->euid == 0) {
        return 1;
    }
    return task_proc(sender)->uid == task_proc(target)->uid;
}

static void unix_proc_force_terminate_task(task_t* target, int sig)
{
    thread_t* thread;

    proc_t* proc = task_proc(target);
    if (!target || !proc || proc->exited) {
        return;
    }

    unix_proc_close_fds(target);
    proc->exit_code = 128 + sig;
    proc->exited = 1;
    target->doomed = 1;
    /* Только фатальный сигнал: смерть придёт на чекпойнте. */
    proc->sig_pending = (1u << (uint32_t)sig);
    proc->sig_in_handler = 0;
    unix_proc_notify_waiters(target->parent_task_id);
    if (target->parent_task_id) {
        task_t* parent = task_find_by_id(target->parent_task_id);
        if (parent && task_proc(parent)) {
            task_proc(parent)->sig_pending |= (1u << UNIX_SIGCHLD);
        }
    }

    /*
     * Никакой асинхронной пометки DEAD. Поток, бегущий в ядре, может
     * держать спящий замок — Giant при перезахвате, замок ext2 через весь
     * дисковый ввод-вывод, — и пометить его мёртвым со стороны значит
     * оставить замок трупу: каждый следующий вход в сисколл повисал
     * нетаймированным сном на очереди kmutex, и вся система вставала.
     * Найдено по подписи «все спят с armed=0, timed=0» после истечения
     * OOM-окна — и стало частым ровно тогда, когда замок ext2 стал спящим
     * и окно «жертва держит kmutex» выросло с микросекунд до миллисекунд.
     *
     * Смерть — только в собственном контексте потока, на чекпойнте
     * (вход в сисколл, ast), где выходной путь отдаёт всё, что держал.
     * Спящих будим, чтобы они до чекпойнта дошли; ложное пробуждение
     * циклы ожидания переживают по устройству.
     */
    TAILQ_FOREACH(thread, &target->threads, task_link) {
        if (thread_is_dead(thread)) {
            continue;
        }
        if (thread->waitq_owner) {
            waitq_wake_thread(thread->waitq_owner, thread);
        }
    }
}

void unix_proc_notify_waiters(uint64_t parent_task_id)
{
    if (!parent_task_id) {
        return;
    }
    task_t* parent = task_find_by_id(parent_task_id);
    if (parent && task_proc(parent)) {
        waitq_wake_all(&task_proc(parent)->child_waitq);
    }
}

void unix_proc_close_fds(task_t* task)
{
    if (!task) {
        return;
    }
    proc_t* proc = task_proc(task);
    if (!proc) {
        return;
    }
    for (int fd = 0; fd < PROC_MAX_FD; fd++) {
        if (proc->fd_table[fd]) {
            unix_fd_release(task, fd);
        }
    }
}

uint64_t unix_proc_exit(uint64_t status)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    if (task && proc) {
        unix_proc_close_fds(task);
        proc->exit_code = (int32_t)status;
        proc->exited = 1;
        unix_proc_notify_waiters(task->parent_task_id);
        /* Send SIGCHLD to parent — use bitmask so it's never lost. */
        if (task->parent_task_id) {
            task_t* parent = task_find_by_id(task->parent_task_id);
            if (parent && task_proc(parent)) {
                task_proc(parent)->sig_pending |= (1u << UNIX_SIGCHLD);
            }
        }
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
    proc_t* proc = task_proc(task);
    thread_t* thr = thread_get_current();
    if (!task || !thr) {
        return;
    }
    if (!proc->sig_pending || proc->sig_in_handler) {
        return;
    }

    /* Find the lowest-numbered pending signal (bitmask model). */
    uint32_t sig = 0;
    for (uint32_t i = 1; i <= (uint32_t)UNIX_SIG_MAX; i++) {
        if (proc->sig_pending & (1u << i)) {
            sig = i;
            break;
        }
    }
    if (sig == 0) {
        return;
    }

    uint64_t handler = proc->sigaction[sig].handler;
    uint64_t restorer = proc->sigaction[sig].restorer;
    interrupt_frame_t* frame = (interrupt_frame_t*)thr->arch_specific;
    if (!frame) {
        return;
    }

    /* Clear this signal from the pending bitmask before potential re-entry. */
    proc->sig_pending &= ~(1u << sig);

    /* Linux guest tasks (BusyBox ash included) install a SIGCHLD handler, but
     * RodNIX does not yet provide a fully Linux-compatible signal frame and
     * rt_sigreturn path.  Delivering SIGCHLD through the partial signal ABI
     * corrupts the return path after wait4().  For now, keep SIGCHLD
     * wait-driven only for Linux ABI tasks and rely on wait4()/waitpid(). */
    if (sig == UNIX_SIGCHLD && task_get_abi(task) == TASK_ABI_LINUX) {
        return;
    }

    if (handler == UNIX_SIG_IGN) {
        return;
    }

    /* POSIX: SIGCHLD default action is to ignore (not terminate). */
    if (handler <= UNIX_SIG_DFL && sig == UNIX_SIGCHLD) {
        return;
    }

    if (handler <= UNIX_SIG_DFL || sig == UNIX_SIGKILL) {
        /* Stop/continue signals (SIGCONT=18, SIGSTOP=19, SIGTSTP=20,
         * SIGTTIN=21, SIGTTOU=22) with SIG_DFL disposition should stop or
         * continue the process.  We do not implement process stopping, so
         * silently discard them rather than terminating — ash forkchild resets
         * these to SIG_DFL before exec and may route them through process-group
         * kill; terminating would misreport the child's exit status. */
        enum { SIGCONT = 18, SIGSTOP = 19, SIGTSTP = 20, SIGTTIN = 21, SIGTTOU = 22 };
        if (handler <= UNIX_SIG_DFL && sig != UNIX_SIGKILL &&
            (sig == SIGCONT || sig == SIGSTOP || sig == SIGTSTP ||
             sig == SIGTTIN || sig == SIGTTOU)) {
            return;
        }
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
    proc->sig_in_handler = 1;
    /* sig_pending already has this signal cleared; other signals remain queued. */
}

uint64_t unix_proc_kill(uint64_t pid, uint64_t signum)
{
    task_t* self = task_get_current();
    int sig = (int)signum;
    int64_t spid = (int64_t)pid;

    if (!self || sig < 0 || sig > UNIX_SIG_MAX) {
        return (uint64_t)RDNX_E_INVALID;
    }

    /* pid == 0: signal to current process group.
     * pid < 0:  signal to process group |pid|.
     * Neither has a full process-group model yet.  For sig==0 (existence probe)
     * return success immediately.  For real signals, deliver to self if our
     * process_group_id matches, otherwise accept silently (no-op stub). */
    if (spid <= 0) {
        if (sig == 0) {
            return (uint64_t)RDNX_OK;
        }
        proc_t* self_proc = task_proc(self);
        uint64_t target_pgid = (spid == 0) ? self_proc->process_group_id
                                            : (uint64_t)(-spid);
        if (self_proc->process_group_id == target_pgid) {
            self_proc->sig_pending |= (1u << (uint32_t)sig);
            unix_proc_signal_checkpoint();
        }
        return (uint64_t)RDNX_OK;
    }

    task_t* target = task_find_by_id((uint64_t)spid);
    if (!target) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    if (!unix_signal_may_send(self, target)) {
        return (uint64_t)RDNX_E_DENIED;
    }
    if (sig == 0) {
        return (uint64_t)RDNX_OK;
    }

    if (target != self && (sig == UNIX_SIGTERM || sig == UNIX_SIGKILL)) {
        unix_proc_force_terminate_task(target, sig);
        return (uint64_t)RDNX_OK;
    }

    task_proc(target)->sig_pending |= (1u << (uint32_t)sig);
    if (target == self) {
        unix_proc_signal_checkpoint();
    }
    return (uint64_t)RDNX_OK;
}

uint64_t unix_proc_sigaction(uint64_t signum, uint64_t user_act_ptr, uint64_t user_oldact_ptr)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    int sig = (int)signum;
    if (!task || sig <= 0 || sig > UNIX_SIG_MAX || sig == UNIX_SIGKILL) {
        return (uint64_t)RDNX_E_INVALID;
    }

    if (user_oldact_ptr) {
        if (!unix_user_range_ok((void*)(uintptr_t)user_oldact_ptr, sizeof(unix_sigaction_u_t))) {
            return (uint64_t)RDNX_E_INVALID;
        }
        unix_sigaction_u_t kold;
        kold.sa_handler  = proc->sigaction[sig].handler;
        kold.sa_flags    = proc->sigaction[sig].flags;
        kold.sa_restorer = proc->sigaction[sig].restorer;
        kold.sa_mask     = proc->sigaction[sig].mask;
        if (unix_copy_to_user((void*)(uintptr_t)user_oldact_ptr, &kold, sizeof(kold)) != RDNX_OK) {
            return (uint64_t)RDNX_E_INVALID;
        }
    }

    if (user_act_ptr) {
        if (!unix_user_range_ok((void*)(uintptr_t)user_act_ptr, sizeof(unix_sigaction_u_t))) {
            return (uint64_t)RDNX_E_INVALID;
        }
        unix_sigaction_u_t knew;
        if (unix_copy_from_user(&knew, (const void*)(uintptr_t)user_act_ptr, sizeof(knew)) != RDNX_OK) {
            return (uint64_t)RDNX_E_INVALID;
        }
        proc->sigaction[sig].handler  = knew.sa_handler;
        proc->sigaction[sig].flags    = knew.sa_flags;
        proc->sigaction[sig].restorer = knew.sa_restorer;
        proc->sigaction[sig].mask     = knew.sa_mask;
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

uint64_t unix_proc_waitpid(uint64_t pid, uint64_t user_status_ptr, uint64_t flags)
{
    /* CT-004/CT-005/CT-006/CT-045 contract point. */
    task_t* self = task_get_current();
    bool wait_any_child = (pid == UINT64_MAX);
    bool nohang = (flags & UNIX_WNOHANG) != 0;
    if (!self || pid == 0 || (flags & ~(uint64_t)UNIX_WNOHANG) != 0) {
        return (uint64_t)RDNX_E_INVALID;
    }

    if (user_status_ptr && !unix_user_range_ok((void*)(uintptr_t)user_status_ptr, sizeof(int))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    task_t* child = NULL;
    if (!wait_any_child) {
        child = task_find_by_id(pid);
        if (!child) {
            return (uint64_t)RDNX_E_NOTFOUND;
        }
        if (child->parent_task_id != self->task_id) {
            return (uint64_t)RDNX_E_DENIED;
        }
    }

    /* Block until child exits. Wake-up is delivered via unix_proc_notify_waiters()
     * which calls waitq_wake_all() on the parent's proc->child_waitq when a child transitions
     * to ZOMBIE. Re-check after each wakeup in case of spurious wakeups or
     * multiple children. Short timeout (100 ms) handles the TOCTOU race where
     * the child exits between the exited-check and waitq_wait enqueue. */
    for (;;) {
        if (wait_any_child) {
            child = proc_find_child_by_parent(self->task_id, 1, 0);
            if (child) {
                break;
            }
            if (!proc_find_child_by_parent(self->task_id, 0, 0)) {
                return (uint64_t)RDNX_E_NOTFOUND;
            }
            if (nohang) {
                /* Дети живы, зомби нет: WNOHANG велит вернуть 0 сразу. */
                return 0;
            }
            (void)waitq_wait(&task_proc(self)->child_waitq, 100);
            continue;
        }

        bool child_exited = task_proc(child)->exited ||
                            (child->state == TASK_STATE_ZOMBIE) ||
                            (child->state == TASK_STATE_DEAD);
        if (child_exited) {
            break;
        }
        /* Check child is still in the registry (not freed by a race) */
        if (!task_find_by_id(pid)) {
            return (uint64_t)RDNX_E_NOTFOUND;
        }
        if (nohang) {
            /* Ребёнок жив: WNOHANG велит вернуть 0 сразу. */
            return 0;
        }
        (void)waitq_wait(&task_proc(self)->child_waitq, 100);
    }

    if (task_proc(child)->waited) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }

    task_proc(child)->waited = 1;
    if (user_status_ptr) {
        int code = task_proc(child)->exit_code;
        (void)unix_copy_to_user((void*)(uintptr_t)user_status_ptr, &code, sizeof(int));
    }
    bool destroy_now = (child->thread_count == 0);
    if (destroy_now) {
        child->state = TASK_STATE_DEAD;
        task_destroy(child);
    }
    return wait_any_child ? child->task_id : pid;
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
    if (child) {
        /* Память наследует политику родителя: класс объектов по умолчанию и
         * полосу убийства. Ребёнок волен опустить себя ниже — сисколлом. */
        child->vm_class_default = parent->vm_class_default;
        child->mem_band = parent->mem_band;
    }
    if (!child) {
        return (uint64_t)RDNX_E_NOMEM;
    }
    child->state = TASK_STATE_READY;
    child->parent_task_id = parent->task_id;
    proc_set_ids(task_proc(child), task_proc(parent)->uid, task_proc(parent)->gid, task_proc(parent)->euid, task_proc(parent)->egid);
    (void)proc_set_supp_groups(task_proc(child), task_proc(parent)->supp_groups, task_proc(parent)->supp_group_count);
    task_set_abi(child, task_get_abi(parent));
    task_proc(child)->session_id = task_proc(parent)->session_id;
    task_proc(child)->process_group_id = task_proc(parent)->process_group_id;
    child->tls_fs_base = parent->tls_fs_base;
    task_proc(child)->umask = task_proc(parent)->umask;
    strncpy(task_proc(child)->cwd, task_proc(parent)->cwd, sizeof(task_proc(child)->cwd) - 1);
    task_proc(child)->cwd[sizeof(task_proc(child)->cwd) - 1] = '\0';

    if (unix_clone_fds_for_spawn(parent, child) != RDNX_OK) {
        task_destroy(child);
        return (uint64_t)RDNX_E_GENERIC;
    }

    pmap_t child_pmap = pmap_create();
    if (!child_pmap) {
        task_destroy(child);
        return (uint64_t)RDNX_E_NOMEM;
    }
    child->address_space = child_pmap;
    if (vm_task_fork_clone(parent, child, child_pmap) != RDNX_OK) {
        task_destroy(child);
        return (uint64_t)RDNX_E_GENERIC;
    }

    thread_t* child_thread = thread_create_user_clone(child, frame);
    if (!child_thread) {
        task_destroy(child);
        return (uint64_t)RDNX_E_NOMEM;
    }
    /* Inherit FS.Base so the child's first context-switch sets this CPU's percpu.tls_fs_base
     * correctly; without this any IRQ before the child's first syscall clears TLS. */
    child_thread->tls_fs_base = child->tls_fs_base;
    child_thread->priority = self_thread->priority;
    child_thread->base_priority = self_thread->base_priority;
    child_thread->dyn_priority = self_thread->dyn_priority;
    scheduler_add_thread(child_thread);
    return (uint64_t)child->task_id;
}

uint64_t unix_time_nanosleep(uint64_t user_req_ptr, uint64_t user_rem_ptr)
{
    if (!user_req_ptr || !unix_user_range_ok((void*)(uintptr_t)user_req_ptr, sizeof(unix_timespec_u_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_rem_ptr && !unix_user_range_ok((void*)(uintptr_t)user_rem_ptr, sizeof(unix_timespec_u_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    unix_timespec_u_t kreq;
    if (unix_copy_from_user(&kreq, (const void*)(uintptr_t)user_req_ptr, sizeof(kreq)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (kreq.tv_sec < 0 || kreq.tv_nsec < 0 || kreq.tv_nsec >= 1000000000LL) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint64_t ms_from_sec = (uint64_t)kreq.tv_sec * 1000ULL;
    uint64_t ms_from_nsec = (uint64_t)kreq.tv_nsec / 1000000ULL;
    uint64_t total_ms = ms_from_sec + ms_from_nsec;
    if ((kreq.tv_nsec % 1000000LL) != 0) {
        total_ms++;
    }

    if (total_ms > 0) {
        scheduler_sleep(total_ms);
    } else {
        scheduler_yield();
    }

    if (user_rem_ptr) {
        unix_timespec_u_t krem = {0, 0};
        (void)unix_copy_to_user((void*)(uintptr_t)user_rem_ptr, &krem, sizeof(krem));
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

/* ============================================================
 * CLONE flags (subset, compatible with Linux/POSIX pthreads)
 * ============================================================ */
#define CLONE_VM            0x00000100u  /* share address space */
#define CLONE_FS            0x00000200u  /* share filesystem state */
#define CLONE_FILES         0x00000400u  /* share fd table */
#define CLONE_SIGHAND       0x00000800u  /* share signal handlers */
#define CLONE_THREAD        0x00010000u  /* same thread group as parent */
#define CLONE_SETTLS        0x00080000u  /* set FS.Base = newtls */
#define CLONE_PARENT_SETTID 0x00100000u  /* write tid to *ptid in parent */
#define CLONE_CHILD_CLEARTID 0x00200000u /* zero *ctid and futex_wake on exit */
#define CLONE_CHILD_SETTID  0x01000000u  /* write tid to *ctid in child */

uint64_t unix_proc_clone(uint64_t flags,
                         uint64_t child_stack,
                         uint64_t ptid,
                         uint64_t ctid,
                         uint64_t newtls)
{
    task_t* task = task_get_current();
    thread_t* self = thread_get_current();
    if (!task || !self) {
        return (uint64_t)RDNX_E_INVALID;
    }

    /* For now only support thread creation (CLONE_VM required). */
    if (!(flags & CLONE_VM)) {
        return (uint64_t)RDNX_E_UNSUPPORTED;
    }
    if (!child_stack) {
        return (uint64_t)RDNX_E_INVALID;
    }

    interrupt_frame_t* frame = (interrupt_frame_t*)self->arch_specific;
    if (!unix_frame_on_thread_stack(self, frame)) {
        frame = (interrupt_frame_t*)(uintptr_t)self->context.stack_pointer;
    }
    if (!unix_frame_on_thread_stack(self, frame)) {
        return (uint64_t)RDNX_E_GENERIC;
    }

    uint64_t tls = (flags & CLONE_SETTLS) ? newtls : 0;

    thread_t* child = thread_create_user_thread(task, frame, child_stack, tls);
    if (!child) {
        return (uint64_t)RDNX_E_NOMEM;
    }

    uint64_t tid = child->thread_id;

    /* CLONE_PARENT_SETTID: write tid to parent address before child runs. */
    if ((flags & CLONE_PARENT_SETTID) && ptid) {
        int32_t* p = (int32_t*)(uintptr_t)ptid;
        if (unix_user_range_ok(p, sizeof(*p))) {
            *p = (int32_t)tid;
        }
    }

    /* CLONE_CHILD_SETTID / CLONE_CHILD_CLEARTID: handled by child on first run.
     * Store ctid in child thread so it can write its own tid after scheduling. */
    if ((flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) && ctid) {
        child->clear_tid_ptr = (uint64_t*)(uintptr_t)ctid;
    }

    /* If CLONE_CHILD_SETTID: patch the child's interrupt frame so that,
     * when the child's first syscall returns, we write the tid.
     * Simpler: write it now since we share address space. */
    if ((flags & CLONE_CHILD_SETTID) && ctid) {
        int32_t* cp = (int32_t*)(uintptr_t)ctid;
        if (unix_user_range_ok(cp, sizeof(*cp))) {
            *cp = (int32_t)tid;
        }
    }

    child->priority = self->priority;
    child->base_priority = self->base_priority;
    child->dyn_priority = self->dyn_priority;
    scheduler_add_thread(child);

    return tid;
}

uint64_t unix_proc_thread_exit(uint64_t status)
{
    (void)status;
    thread_t* cur = thread_get_current();
    task_t* task = task_get_current();

    if (cur && cur->clear_tid_ptr) {
        /* CLONE_CHILD_CLEARTID: zero the tid word and wake futex waiters. */
        uint64_t* tidptr = cur->clear_tid_ptr;
        if (unix_user_range_ok(tidptr, sizeof(uint32_t))) {
            *(uint32_t*)(uintptr_t)tidptr = 0;
            /* Wake any thread waiting on this futex (futex_wake(tidptr, 1)). */
            extern uint64_t unix_proc_futex(uint64_t, uint64_t, uint64_t,
                                            uint64_t, uint64_t, uint64_t);
            unix_proc_futex((uint64_t)(uintptr_t)tidptr,
                            1 /* FUTEX_WAKE */, 1, 0, 0, 0);
        }
        cur->clear_tid_ptr = NULL;
    }

    /* If this is the last thread, close fds and notify parent like proc_exit. */
    if (task) {
        uint32_t live = 0;
        thread_t* t;
        TAILQ_FOREACH(t, &task->threads, task_link) {
            if (t != cur && !thread_is_dead(t)) {
                live++;
            }
        }
        if (live == 0) {
            unix_proc_close_fds(task);
            task_proc(task)->exit_code = (int32_t)status;
            task_proc(task)->exited = 1;
            unix_proc_notify_waiters(task->parent_task_id);
            if (task->parent_task_id) {
                task_t* parent = task_find_by_id(task->parent_task_id);
                if (parent) {
                    task_proc(parent)->sig_pending |= (1u << UNIX_SIGCHLD);
                }
            }
        }
    }

    scheduler_exit_current();
    return 0;
}

/*
 * Труп-в-процессе: выход уже начат — своим exit или force-снятием — и поток
 * дорабатывает спуск смерти до ZOMBIE. Force по такой задаче молча ничего
 * не делает (proc->exited уже стоит), поэтому новое OOM-окно на неё — 200 мс
 * ожидания по трупу; отбор жертв обязан таких пропускать.
 */
int unix_proc_task_dying(const task_t* task)
{
    if (!task) {
        return 0;
    }
    if (task->doomed) {
        return 1;
    }
    const proc_t* proc = task_proc(task);
    return proc && proc->exited;
}

/*
 * Вход для убийцы по полосам (kernel/oom.c). force=0 — вежливое SIGTERM в
 * ожидающие сигналы: окно на сброс, приложение вправе успеть записать
 * проект. force=1 — немедленное снятие задачи.
 */
int unix_proc_oom_signal(uint64_t task_id, int force)
{
    task_t* target = task_find_by_id(task_id);
    if (!target || target->state == TASK_STATE_DEAD ||
        target->state == TASK_STATE_ZOMBIE) {
        return RDNX_E_NOTFOUND;
    }
    if (force) {
        unix_proc_force_terminate_task(target, UNIX_SIGKILL);
        return RDNX_OK;
    }
    proc_t* p = task_proc(target);
    if (!p) {
        return RDNX_E_NOTFOUND;
    }
    p->sig_pending |= (1u << UNIX_SIGTERM);
    return RDNX_OK;
}
