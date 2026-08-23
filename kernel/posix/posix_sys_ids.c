#include "posix_sys_ids.h"
#include "../core/task.h"
#include "../security.h"
#include "../unix/unix_layer.h"
#include "../../include/error.h"

int posix_bind_stdio_to_console(task_t* task)
{
    return unix_bind_stdio_to_console(task);
}

uint64_t posix_nosys(uint64_t a1,
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

uint64_t posix_getpid(uint64_t a1,
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

uint64_t posix_getuid(uint64_t a1,
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
    return task ? task_proc(task)->uid : 0;
}

uint64_t posix_geteuid(uint64_t a1,
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
    return task ? task_proc(task)->euid : 0;
}

uint64_t posix_getgid(uint64_t a1,
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
    return task ? task_proc(task)->gid : 0;
}

uint64_t posix_getegid(uint64_t a1,
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
    return task ? task_proc(task)->egid : 0;
}

uint64_t posix_setuid(uint64_t a1,
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
    proc_t* proc = task_proc(task);
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    proc_set_ids(proc, (uint32_t)a1, proc->gid, proc->euid, proc->egid);
    return (uint64_t)RDNX_OK;
}

uint64_t posix_seteuid(uint64_t a1,
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
    proc_t* proc = task_proc(task);
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    proc_set_ids(proc, proc->uid, proc->gid, (uint32_t)a1, proc->egid);
    return (uint64_t)RDNX_OK;
}

uint64_t posix_setgid(uint64_t a1,
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
    proc_t* proc = task_proc(task);
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    proc_set_ids(proc, proc->uid, (uint32_t)a1, proc->euid, proc->egid);
    return (uint64_t)RDNX_OK;
}

uint64_t posix_setegid(uint64_t a1,
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
    proc_t* proc = task_proc(task);
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    proc_set_ids(proc, proc->uid, proc->gid, proc->euid, (uint32_t)a1);
    return (uint64_t)RDNX_OK;
}

uint64_t posix_getgroups(uint64_t a1,
                         uint64_t a2,
                         uint64_t a3,
                         uint64_t a4,
                         uint64_t a5,
                         uint64_t a6)
{
    uint32_t size = (uint32_t)a1;
    uint32_t* user_gids = (uint32_t*)(uintptr_t)a2;
    task_t* task;
    uint32_t count;
    uint32_t gids[PROC_MAX_SUPP_GROUPS];

    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;

    task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    count = proc_get_supp_group_count(task_proc(task));
    if (size == 0) {
        return (uint64_t)count;
    }
    if (!user_gids || size < count) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (proc_copy_supp_groups(task_proc(task), gids, PROC_MAX_SUPP_GROUPS) < 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (count > 0 &&
        unix_copy_to_user((void*)(uintptr_t)user_gids, gids, (size_t)count * sizeof(uint32_t)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)count;
}

uint64_t posix_setgroups(uint64_t a1,
                         uint64_t a2,
                         uint64_t a3,
                         uint64_t a4,
                         uint64_t a5,
                         uint64_t a6)
{
    uint32_t count = (uint32_t)a1;
    const uint32_t* user_gids = (const uint32_t*)(uintptr_t)a2;
    uint32_t gids[PROC_MAX_SUPP_GROUPS];
    task_t* task;

    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;

    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)RDNX_E_DENIED;
    }
    if (count > PROC_MAX_SUPP_GROUPS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (count > 0 && !user_gids) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (count > 0 &&
        unix_copy_from_user(gids, (const void*)(uintptr_t)user_gids, (size_t)count * sizeof(uint32_t)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)proc_set_supp_groups(task_proc(task), gids, count);
}
