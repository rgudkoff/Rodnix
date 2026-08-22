/**
 * @file file.c
 * @brief Объект описания открытого файла: аллокация и подсчёт ссылок.
 */

#include "../../../include/sys/file.h"
#include "../../../include/error.h"
#include "../../../include/common.h"
#include "../proc.h"
#include "../../fabric/spin.h"
#include "../../../lib/heap.h"

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
    if (f) {
        f->refs++;
    }
}

void rdnx_file_put(rdnx_file_t* f)
{
    if (!f || f->refs == 0) {
        return;
    }
    if (--f->refs > 0) {
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
