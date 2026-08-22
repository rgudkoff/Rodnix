/**
 * @file unix_proc.c
 * @brief Жизненный цикл и базовые операции UNIX-персоналии процесса
 *
 * Всё, что раньше жило прямо в task_t и обслуживалось kernel/task.c:
 * креды, дескрипторы, cwd, статус завершения, сигналы, brk/mmap-раскладка.
 * Ядро вызывает отсюда только proc_attach() / proc_detach().
 */

#include "../proc.h"
#include "../unix_layer.h"
#include "../../../lib/heap.h"
#include "../../../sched/waitq.h"
#include "../../../include/common.h"
#include "../../../include/error.h"

/* ============================================================================
 * Жизненный цикл
 * ============================================================================ */

proc_t* proc_attach(task_t* task)
{
    if (!task) {
        return NULL;
    }
    if (task->proc) {
        return task->proc;
    }

    proc_t* proc = (proc_t*)kmalloc(sizeof(proc_t));
    if (!proc) {
        return NULL;
    }
    memset(proc, 0, sizeof(*proc));

    proc->task = task;
    proc->session_id = task->task_id;
    proc->process_group_id = task->task_id;
    proc->umask = 0022;
    proc->cwd[0] = '/';
    proc->cwd[1] = '\0';
    waitq_init(&proc->child_waitq, "child_wait");

    task->proc = proc;
    return proc;
}

void proc_detach(task_t* task)
{
    if (!task || !task->proc) {
        return;
    }
    proc_t* proc = task->proc;
    for (int i = 0; i < PROC_MAX_FD; i++) {
        if (proc->fd_table[i]) {
            unix_fd_release(task, i);
        }
    }
    task->proc = NULL;
    proc->task = NULL;
    kfree(proc);
}

proc_t* proc_current(void)
{
    return task_proc(task_get_current());
}

/* ============================================================================
 * Креды
 * ============================================================================ */

void proc_set_ids(proc_t* proc, uint32_t uid, uint32_t gid, uint32_t euid, uint32_t egid)
{
    if (!proc) {
        return;
    }
    proc->uid = uid;
    proc->gid = gid;
    proc->euid = euid;
    proc->egid = egid;
}

int proc_set_supp_groups(proc_t* proc, const uint32_t* gids, uint32_t count)
{
    if (!proc) {
        return RDNX_E_INVALID;
    }
    if (count > PROC_MAX_SUPP_GROUPS) {
        return RDNX_E_INVALID;
    }
    proc->supp_group_count = 0;
    for (uint32_t i = 0; i < PROC_MAX_SUPP_GROUPS; i++) {
        proc->supp_groups[i] = 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        proc->supp_groups[i] = gids ? gids[i] : 0;
    }
    proc->supp_group_count = count;
    return RDNX_OK;
}

uint32_t proc_get_supp_group_count(const proc_t* proc)
{
    return proc ? proc->supp_group_count : 0;
}

int proc_copy_supp_groups(const proc_t* proc, uint32_t* out_gids, uint32_t max_count)
{
    if (!proc) {
        return RDNX_E_INVALID;
    }
    if (proc->supp_group_count > max_count) {
        return RDNX_E_INVALID;
    }
    if (proc->supp_group_count > 0 && !out_gids) {
        return RDNX_E_INVALID;
    }
    for (uint32_t i = 0; i < proc->supp_group_count; i++) {
        out_gids[i] = proc->supp_groups[i];
    }
    return (int)proc->supp_group_count;
}

int proc_in_group(const proc_t* proc, uint32_t gid)
{
    if (!proc) {
        return 0;
    }
    if (proc->gid == gid || proc->egid == gid) {
        return 1;
    }
    for (uint32_t i = 0; i < proc->supp_group_count; i++) {
        if (proc->supp_groups[i] == gid) {
            return 1;
        }
    }
    return 0;
}

uint32_t proc_get_euid(const proc_t* proc)
{
    return proc ? proc->euid : 0;
}

uint32_t proc_get_egid(const proc_t* proc)
{
    return proc ? proc->egid : 0;
}

/* ============================================================================
 * Дескрипторы
 * ============================================================================ */

int proc_fd_alloc(proc_t* proc, void* handle)
{
    if (!proc || !handle) {
        return RDNX_E_INVALID;
    }
    for (int i = 0; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i]) {
            proc->fd_table[i] = handle;
            proc->fd_flags[i] = 0;
            proc->fd_kind[i] = 0;
            return i;
        }
    }
    return RDNX_E_BUSY;
}

void* proc_fd_get(proc_t* proc, int fd)
{
    if (!proc || fd < 0 || fd >= PROC_MAX_FD) {
        return NULL;
    }
    return proc->fd_table[fd];
}

int proc_fd_close(proc_t* proc, int fd)
{
    if (!proc || fd < 0 || fd >= PROC_MAX_FD) {
        return RDNX_E_INVALID;
    }
    if (!proc->fd_table[fd]) {
        return RDNX_E_INVALID;
    }
    proc->fd_table[fd] = NULL;
    proc->fd_flags[fd] = 0;
    proc->fd_kind[fd] = 0;
    return RDNX_OK;
}

/* ============================================================================
 * Иерархия процессов
 * ============================================================================ */

typedef struct {
    uint64_t parent_task_id;
    int require_exited;
    int include_waited;
    task_t* found;
} proc_child_scan_t;

static void proc_child_scan(const task_t* task, void* ctx)
{
    proc_child_scan_t* scan = (proc_child_scan_t*)ctx;
    if (scan->found || !task) {
        return;
    }
    if (task->parent_task_id != scan->parent_task_id) {
        return;
    }
    const proc_t* proc = task_proc(task);
    if (!proc) {
        return;
    }
    if (!scan->include_waited && proc->waited) {
        return;
    }
    bool child_exited = proc->exited ||
                        (task->state == TASK_STATE_ZOMBIE) ||
                        (task->state == TASK_STATE_DEAD);
    if (scan->require_exited && !child_exited) {
        return;
    }
    /* task_for_each отдаёт const-указатель ради обхода; сам объект не const. */
    scan->found = (task_t*)task;
}

task_t* proc_find_child_by_parent(uint64_t parent_task_id, int require_exited, int include_waited)
{
    proc_child_scan_t scan = {
        .parent_task_id = parent_task_id,
        .require_exited = require_exited,
        .include_waited = include_waited,
        .found = NULL
    };
    task_for_each(proc_child_scan, &scan);
    return scan.found;
}
