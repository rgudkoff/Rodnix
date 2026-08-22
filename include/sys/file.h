/**
 * @file file.h
 * @brief Единое описание открытого файла и таблица операций над ним.
 */

#ifndef _RODNIX_SYS_FILE_H
#define _RODNIX_SYS_FILE_H

#include <stddef.h>
#include <stdint.h>
#include "../../fs/vfs.h"      /* vfs_stat_t */

struct task;
typedef struct task task_t;

struct rdnx_proc;
typedef struct rdnx_proc proc_t;

typedef struct rdnx_file rdnx_file_t;
struct poll_ctx;

/* Биты готовности. Раньше это был безымянный enum внутри unix_fd.c
 * (строка 51); теперь они часть контракта file_ops.poll, поэтому живут
 * рядом с ним. Значения не меняются — userland ABI прежний. */
enum {
    UNIX_POLLIN   = 0x0001,
    UNIX_POLLOUT  = 0x0004,
    UNIX_POLLERR  = 0x0008,
    UNIX_POLLHUP  = 0x0010,
    UNIX_POLLNVAL = 0x0020
};

typedef struct file_ops {
    const char* name;   /* "vfs" / "pipe" / "socket" — для отладки и fdinfo */
    int64_t (*read)  (rdnx_file_t* f, void* kbuf, size_t len);
    int64_t (*write) (rdnx_file_t* f, const void* kbuf, size_t len);
    int64_t (*seek)  (rdnx_file_t* f, int64_t off, int whence);
    int     (*ioctl) (rdnx_file_t* f, uint64_t req, void* karg);
    int     (*stat)  (rdnx_file_t* f, vfs_stat_t* out);
    short   (*poll)  (rdnx_file_t* f, short events, struct poll_ctx* pc);
    int     (*close) (rdnx_file_t* f);   /* только на последней ссылке */
} file_ops_t;

/* Временное поле kind: пока не все обработчики переведены на ops.
 * Удаляется в Task 7 вместе с UNIX_FD_KIND_*. */
struct rdnx_file {
    const file_ops_t* ops;
    uint32_t  refs;
    uint32_t  status_flags;   /* O_APPEND / O_NONBLOCK — свойство описания */
    int64_t   pos;
    void*     priv;           /* vfs_file_t* / unix_pipe_t* / net_socket_t* */
    uint8_t   kind;           /* TEMP: UNIX_FD_KIND_* на время миграции */
};

/* Статусные флаги описания. Значения совпадают с UNIX_FD_NONBLOCK,
 * чтобы перенос из fd_flags был механическим. */
#define RDNX_F_NONBLOCK  0x01u
#define RDNX_F_APPEND    0x02u

rdnx_file_t* rdnx_file_alloc(const file_ops_t* ops, void* priv);
void         rdnx_file_ref(rdnx_file_t* f);
void         rdnx_file_put(rdnx_file_t* f);

int          fd_install(proc_t* proc, rdnx_file_t* f);
rdnx_file_t* fd_get(proc_t* proc, int fd);
void         fd_put(rdnx_file_t* f);
int          fd_close(proc_t* proc, int fd);

#endif /* _RODNIX_SYS_FILE_H */
