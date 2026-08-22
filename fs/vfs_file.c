/**
 * @file vfs_file.c
 * @brief Реализация file_ops поверх VFS.
 */

#include "vfs.h"
#include "../include/sys/file.h"
#include "../include/error.h"
#include "../lib/heap.h"

static int64_t vfs_fop_read(rdnx_file_t* f, void* kbuf, size_t len)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_E_INVALID;
    }
    vf->pos = (size_t)f->pos;
    int ret = vfs_read(vf, kbuf, len);
    f->pos = (int64_t)vf->pos;
    return (int64_t)ret;
}

static int64_t vfs_fop_write(rdnx_file_t* f, const void* kbuf, size_t len)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_E_INVALID;
    }
    vf->pos = (size_t)f->pos;
    int ret = vfs_write(vf, kbuf, len);
    f->pos = (int64_t)vf->pos;
    return (int64_t)ret;
}

static int64_t vfs_fop_seek(rdnx_file_t* f, int64_t off, int whence)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_E_INVALID;
    }
    uint64_t out = 0;
    vf->pos = (size_t)f->pos;
    int ret = vfs_seek(vf, off, whence, &out);
    if (ret != RDNX_OK) {
        return (int64_t)ret;
    }
    f->pos  = (int64_t)out;
    vf->pos = (size_t)out;
    return (int64_t)out;
}

static int vfs_fop_stat(rdnx_file_t* f, vfs_stat_t* out)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_E_INVALID;
    }
    return vfs_fstat(vf, out);
}

static int vfs_fop_close(rdnx_file_t* f)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_OK;
    }
    (void)vfs_close(vf);
    kfree(vf);
    f->priv = NULL;
    return RDNX_OK;
}

const file_ops_t vfs_fileops = {
    .name  = "vfs",
    .read  = vfs_fop_read,
    .write = vfs_fop_write,
    .seek  = vfs_fop_seek,
    .ioctl = NULL,     /* Task 4 */
    .stat  = vfs_fop_stat,
    .poll  = NULL,     /* Task 5 */
    .close = vfs_fop_close,
};

rdnx_file_t* vfs_file_open(const char* path, int vfs_flags)
{
    vfs_file_t* vf = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!vf) {
        return NULL;
    }
    if (vfs_open(path, vfs_flags, vf) != RDNX_OK) {
        kfree(vf);
        return NULL;
    }
    rdnx_file_t* f = rdnx_file_alloc(&vfs_fileops, vf);
    if (!f) {
        (void)vfs_close(vf);
        kfree(vf);
        return NULL;
    }
    f->pos = (int64_t)vf->pos;
    return f;
}
