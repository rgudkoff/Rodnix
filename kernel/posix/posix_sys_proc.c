#include "posix_sys_proc.h"
#include "../unix/unix_layer.h"
#include "../../sched/scheduler.h"
#include "../../include/error.h"

uint64_t posix_exit(uint64_t a1,
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
    return unix_proc_exit(a1);
}

uint64_t posix_spawn(uint64_t a1,
                            uint64_t a2,
                            uint64_t a3,
                            uint64_t a4,
                            uint64_t a5,
                            uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    return unix_proc_spawn(a1, a2, a3);
}

uint64_t posix_waitpid(uint64_t a1,
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
    return unix_proc_waitpid(a1, a2);
}
uint64_t posix_fork(uint64_t a1,
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
    return unix_proc_fork();
}

uint64_t posix_kill(uint64_t a1,
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
    return unix_proc_kill(a1, a2);
}

uint64_t posix_sigaction(uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    return unix_proc_sigaction(a1, a2, a3);
}

uint64_t posix_sigreturn(uint64_t a1,
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
    return unix_proc_sigreturn();
}

uint64_t posix_futex(uint64_t a1,
                            uint64_t a2,
                            uint64_t a3,
                            uint64_t a4,
                            uint64_t a5,
                            uint64_t a6)
{
    return unix_proc_futex(a1, a2, a3, a4, a5, a6);
}

/*
 * clone(flags, child_stack, ptid, ctid, newtls)
 * a1=flags  a2=child_stack  a3=ptid  a4=ctid  a5=newtls
 */
uint64_t posix_clone(uint64_t a1,
                     uint64_t a2,
                     uint64_t a3,
                     uint64_t a4,
                     uint64_t a5,
                     uint64_t a6)
{
    (void)a6;
    return unix_proc_clone(a1, a2, a3, a4, a5);
}

/*
 * thread_exit(status) — exit current thread only.
 */
uint64_t posix_thread_exit(uint64_t a1,
                           uint64_t a2,
                           uint64_t a3,
                           uint64_t a4,
                           uint64_t a5,
                           uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return unix_proc_thread_exit(a1);
}

/*
 * Перевести текущий поток в класс реального времени и обратно.
 *
 * Никакой политики допуска пока нет — это ручка для обещания этапа 7 и его
 * теста. Настоящая ОС креаторов даст RT через сессию аудио-сервера, а не
 * голым сисколлом; ручка останется под капотом той сессии.
 */
uint64_t posix_schedrt(uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    thread_t* self = thread_get_current();
    if (!self) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (a1) {
        self->sched_class = SCHED_CLASS_REALTIME;
        scheduler_set_bucket(self, SCHED_BUCKET_INTERACTIVE);
    } else {
        self->sched_class = SCHED_CLASS_TIMESHARE;
    }
    return 0;
}

uint64_t posix_threadfaults(uint64_t a1, uint64_t a2, uint64_t a3,
                            uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    thread_t* self = thread_get_current();
    if (!self) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return self->fault_count;
}

/*
 * Полоса убийства при нехватке памяти. Понижать себя может всякий; v0 не
 * охраняет повышение — политика допуска придёт вместе с сессиями аудио.
 */
uint64_t posix_memband(uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if ((int64_t)a1 == -1) {
        return task->mem_band;
    }
    if (a1 > (uint64_t)MEMBAND_CRITICAL) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task->mem_band = (uint8_t)a1;
    return 0;
}
