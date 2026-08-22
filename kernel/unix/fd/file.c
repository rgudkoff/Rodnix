/**
 * @file file.c
 * @brief Объект описания открытого файла: аллокация и подсчёт ссылок.
 */

#include "../../../include/sys/file.h"
#include "../../../include/error.h"
#include "../../../include/common.h"
#include "../proc.h"
#include "../../core/interrupts.h"
#include "../../../lib/heap.h"

/* Refcount races against the timer IRQ: the scheduler preempts unconditionally
 * in kernel mode, and unix_clone_fds_for_spawn() now shares one rdnx_file_t
 * across processes by design, so two runnable tasks touching f->refs is the
 * normal case, not an edge case. Raise IRQL around the read-modify-write,
 * matching the unix_pipe_lock()/unix_pipe_unlock() pattern this replaces
 * (kernel/unix/fd/unix_fd.c) — this kernel is single-core, so blocking all
 * interrupts on this CPU is sufficient to make the increment/decrement atomic. */
static inline irql_t rdnx_file_lock(void)
{
    return set_irql(IRQL_HIGH);
}

static inline void rdnx_file_unlock(irql_t old)
{
    (void)set_irql(old);
}

rdnx_file_t* rdnx_file_alloc(const file_ops_t* ops, void* priv)
{
    if (!ops) {
        return NULL;
    }
    rdnx_file_t* f = (rdnx_file_t*)kmalloc(sizeof(rdnx_file_t));
    if (!f) {
        return NULL;
    }
    memset(f, 0, sizeof(*f));
    f->ops  = ops;
    f->priv = priv;
    f->refs = 1;
    return f;
}

void rdnx_file_ref(rdnx_file_t* f)
{
    if (!f) {
        return;
    }
    irql_t old = rdnx_file_lock();
    f->refs++;
    rdnx_file_unlock(old);
}

void rdnx_file_put(rdnx_file_t* f)
{
    if (!f) {
        return;
    }
    irql_t old = rdnx_file_lock();
    if (f->refs == 0) {
        rdnx_file_unlock(old);
        return;
    }
    uint32_t remaining = --f->refs;
    rdnx_file_unlock(old);
    if (remaining > 0) {
        return;
    }
    if (f->ops && f->ops->close) {
        (void)f->ops->close(f);
    }
    kfree(f);
}

int fd_install(proc_t* proc, rdnx_file_t* f)
{
    if (!proc || !f) {
        return RDNX_E_INVALID;
    }
    return proc_fd_alloc(proc, f);
}

rdnx_file_t* fd_get(proc_t* proc, int fd)
{
    rdnx_file_t* f = (rdnx_file_t*)proc_fd_get(proc, fd);
    if (f) {
        rdnx_file_ref(f);
    }
    return f;
}

void fd_put(rdnx_file_t* f)
{
    rdnx_file_put(f);
}

int fd_close(proc_t* proc, int fd)
{
    rdnx_file_t* f = (rdnx_file_t*)proc_fd_get(proc, fd);
    if (!f) {
        return RDNX_E_INVALID;
    }
    (void)proc_fd_close(proc, fd);
    rdnx_file_put(f);
    return RDNX_OK;
}
