#include "../unix_layer.h"
#include "../../../fs/vfs.h"
#include "../../../include/sys/file.h"
#include "../../../lib/heap.h"
#include "../../../console/tty_console.h"
#include "../../../sched/scheduler.h"
#include "../../core/interrupts.h"
#include "../../../mm/vm_map.h"
#include "../../../net/socket.h"
#include "../../../include/common.h"
#include "../../../include/error.h"
#include "../../../include/gfx.h"
#include "../../../include/fb_abi.h"

enum {
    UNIX_F_DUPFD = 0,
    UNIX_F_GETFD = 1,
    UNIX_F_SETFD = 2,
    UNIX_F_GETFL = 3,
    UNIX_F_SETFL = 4,
    UNIX_F_DUPFD_CLOEXEC = 1030,
    /* fd_flags bits (stored in proc->fd_flags[fd]) — per-descriptor, not
     * inherited by dup(). O_NONBLOCK lives on rdnx_file_t.status_flags
     * instead (RDNX_F_NONBLOCK): the description is shared by dup()/fork(),
     * so O_NONBLOCK must be too, per POSIX. */
    UNIX_FD_CLOEXEC  = 1,
    UNIX_PIPE_CAP = 4096,
    /* POSIX open flags (userland ABI) */
    UNIX_O_RDONLY   = 0x0000,
    UNIX_O_WRONLY   = 0x0001,
    UNIX_O_RDWR     = 0x0002,
    UNIX_O_ACCMODE  = 0x0003,
    UNIX_O_NONBLOCK = 0x0004,
    UNIX_O_APPEND   = 0x0008,
    UNIX_O_SYNC     = 0x0080,
    UNIX_O_NOFOLLOW = 0x0100,
    UNIX_O_CREAT    = 0x0200,
    UNIX_O_TRUNC    = 0x0400,
    UNIX_O_EXCL     = 0x0800,
    UNIX_O_NOCTTY   = 0x8000,
    UNIX_O_CLOEXEC  = 0x00100000,
    /* One-shot open flags — must not appear in F_GETFL result. */
    UNIX_O_ONESHOT  = 0x0600    /* O_CREAT | O_TRUNC */
};

enum {
    UNIX_TTY_IOCTL_ISATTY = 0x7401,
    UNIX_TTY_IOCTL_GETATTR = 0x7402,
    UNIX_TTY_IOCTL_SETATTR = 0x7403,
    UNIX_TTY_IOCTL_GETWINSZ = 0x7404,
    UNIX_TTY_IOCTL_SETWINSZ = 0x7405
};

typedef struct unix_termios_u {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_cc[20];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
} unix_termios_u_t;

typedef struct unix_pollfd_u {
    int32_t fd;
    int16_t events;
    int16_t revents;
} unix_pollfd_u_t;

typedef struct unix_fdset_u {
    uint32_t bits;
} unix_fdset_u_t;

typedef struct unix_timeval_u {
    int64_t tv_sec;
    int64_t tv_usec;
} unix_timeval_u_t;

/* Translate POSIX open(2) flags to VFS_OPEN_* flags. */
static int unix_posix_flags_to_vfs(int posix_flags)
{
    int vfs_flags = 0;
    int accmode = posix_flags & UNIX_O_ACCMODE;
    if (accmode == UNIX_O_RDONLY) {
        vfs_flags |= VFS_OPEN_READ;
    } else if (accmode == UNIX_O_WRONLY) {
        vfs_flags |= VFS_OPEN_WRITE;
    } else { /* O_RDWR */
        vfs_flags |= VFS_OPEN_READ | VFS_OPEN_WRITE;
    }
    if (posix_flags & UNIX_O_CREAT)
        vfs_flags |= VFS_OPEN_CREATE;
    if (posix_flags & UNIX_O_TRUNC)
        vfs_flags |= VFS_OPEN_TRUNC;
    if (posix_flags & UNIX_O_EXCL)
        vfs_flags |= VFS_OPEN_EXCL;
    return vfs_flags;
}

static inline int unix_fdset_test(const unix_fdset_u_t* s, uint32_t fd)
{
    if (!s || fd >= 32u) {
        return 0;
    }
    return (s->bits & (1u << fd)) != 0u;
}

static inline void unix_fdset_set(unix_fdset_u_t* s, uint32_t fd)
{
    if (!s || fd >= 32u) {
        return;
    }
    s->bits |= (1u << fd);
}

typedef struct unix_pipe {
    uint32_t magic;
    uint32_t readers;
    uint32_t writers;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t data[UNIX_PIPE_CAP];
} unix_pipe_t;

#define UNIX_PIPE_MAGIC 0x50495045u

static int unix_is_abs_path(const char* p)
{
    return p && p[0] == '/';
}

/* Build the absolute path string for a VFS node by walking parent pointers.
 * Returns RDNX_OK on success. */
static int unix_vfs_node_path(vfs_node_t* node, char* out, size_t out_sz)
{
    const char* parts[64];
    int count = 0;
    vfs_node_t* cur = node;
    while (cur && cur->parent && cur->parent != cur && count < 64) {
        parts[count++] = cur->name;
        cur = cur->parent;
    }
    size_t p = 0;
    out[p++] = '/';
    for (int i = count - 1; i >= 0; i--) {
        for (const char* s = parts[i]; *s && p + 1 < out_sz; s++) {
            out[p++] = *s;
        }
        if (i > 0 && p + 1 < out_sz) {
            out[p++] = '/';
        }
    }
    if (p >= out_sz) {
        return RDNX_E_INVALID;
    }
    out[p] = '\0';
    return RDNX_OK;
}

enum { UNIX_AT_FDCWD = -100 };

static int unix_path_normalize(const char* in, char* out, size_t out_sz)
{
    char segs[32][UNIX_PATH_MAX];
    int seg_count = 0;
    size_t i = 0;

    if (!in || !out || out_sz < 2) {
        return RDNX_E_INVALID;
    }

    while (in[i] == '/') {
        i++;
    }
    while (in[i] != '\0') {
        char seg[UNIX_PATH_MAX];
        size_t slen = 0;
        while (in[i] != '\0' && in[i] != '/' && slen + 1 < sizeof(seg)) {
            seg[slen++] = in[i++];
        }
        seg[slen] = '\0';
        while (in[i] == '/') {
            i++;
        }

        if (slen == 0 || (slen == 1 && seg[0] == '.')) {
            continue;
        }
        if (slen == 2 && seg[0] == '.' && seg[1] == '.') {
            if (seg_count > 0) {
                seg_count--;
            }
            continue;
        }
        if (seg_count < 32) {
            memcpy(segs[seg_count], seg, slen + 1);
            seg_count++;
        }
    }

    size_t p = 0;
    out[p++] = '/';
    for (int s = 0; s < seg_count; s++) {
        size_t k = 0;
        while (segs[s][k] != '\0') {
            if (p + 1 < out_sz) {
                out[p++] = segs[s][k];
            }
            k++;
        }
        if (s + 1 < seg_count && p + 1 < out_sz) {
            out[p++] = '/';
        }
    }
    if (p == 0) {
        out[0] = '/';
        p = 1;
    }
    out[p] = '\0';
    return RDNX_OK;
}

int unix_resolve_path(const task_t* task, const char* in, char* out, size_t out_sz)
{
    proc_t* proc = task_proc(task);
    char tmp[UNIX_PATH_MAX];
    size_t p = 0;

    if (!task || !in || !out || out_sz < 2) {
        return RDNX_E_INVALID;
    }

    if (unix_is_abs_path(in)) {
        return unix_path_normalize(in, out, out_sz);
    }

    for (size_t i = 0; proc->cwd[i] != '\0' && p + 1 < sizeof(tmp); i++) {
        tmp[p++] = proc->cwd[i];
    }
    if (p == 0 || tmp[p - 1] != '/') {
        tmp[p++] = '/';
    }
    for (size_t i = 0; in[i] != '\0' && p + 1 < sizeof(tmp); i++) {
        tmp[p++] = in[i];
    }
    tmp[p] = '\0';
    return unix_path_normalize(tmp, out, out_sz);
}

int unix_resolve_user_path(const char* user_src, char* out, size_t out_sz)
{
    char in[UNIX_PATH_MAX];
    task_t* task = task_get_current();
    if (!task || unix_copy_user_cstr(in, sizeof(in), user_src) != RDNX_OK) {
        return RDNX_E_INVALID;
    }
    return unix_resolve_path(task, in, out, out_sz);
}

static bool unix_user_io_range_mapped(task_t* task, const void* ptr, size_t len, bool need_write)
{
    if (!task || !ptr) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    vm_map_t* map = (vm_map_t*)task->vm_map;
    if (!map) {
        return false;
    }

    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = 0;
    if (__builtin_add_overflow(start, len - 1, &end)) {
        return false;
    }

    uintptr_t cur = start;
    while (cur <= end) {
        vm_map_entry_t* e = vm_map_lookup(map, cur);
        if (!e) {
            return false;
        }
        if ((e->prot & VM_PROT_READ) == 0) {
            return false;
        }
        if (need_write && (e->prot & VM_PROT_WRITE) == 0) {
            return false;
        }
        if (e->end <= cur) {
            return false;
        }
        cur = (uintptr_t)e->end;
    }

    return true;
}

static inline irql_t unix_pipe_lock(void)
{
    return set_irql(IRQL_HIGH);
}

static inline void unix_pipe_unlock(irql_t old)
{
    (void)set_irql(old);
}

static void unix_pipe_release(unix_pipe_t* p, uint8_t kind)
{
    if (!p || p->magic != UNIX_PIPE_MAGIC) {
        return;
    }

    irql_t old = unix_pipe_lock();
    if (kind == UNIX_FD_KIND_PIPE_R) {
        if (p->readers > 0) {
            p->readers--;
        }
    } else if (kind == UNIX_FD_KIND_PIPE_W) {
        if (p->writers > 0) {
            p->writers--;
        }
    }
    uint32_t readers = p->readers;
    uint32_t writers = p->writers;
    unix_pipe_unlock(old);

    if (readers == 0 && writers == 0) {
        p->magic = 0;
        kfree(p);
    }
}

/* временно в unix_fd.c, переезжает в pipe.c / socket_file.c в Task 3 */
static int pipe_fop_close(rdnx_file_t* f)
{
    unix_pipe_release((unix_pipe_t*)f->priv, f->kind);
    f->priv = NULL;
    return RDNX_OK;
}
static const file_ops_t pipe_fileops_stub = {
    .name = "pipe", .close = pipe_fop_close
};

static int socket_fop_close(rdnx_file_t* f)
{
    net_socket_close((net_socket_t*)f->priv);
    f->priv = NULL;
    return RDNX_OK;
}
static const file_ops_t socket_fileops_stub = {
    .name = "socket", .close = socket_fop_close
};

void unix_fd_release(task_t* task, int fd)
{
    proc_t* proc = task_proc(task);
    if (!proc || fd < 0 || fd >= PROC_MAX_FD) {
        return;
    }
    (void)fd_close(proc, fd);
}

static int unix_fd_dup_into(task_t* task, int oldfd, int newfd)
{
    proc_t* proc = task_proc(task);
    if (!proc || oldfd < 0 || oldfd >= PROC_MAX_FD ||
        newfd < 0 || newfd >= PROC_MAX_FD) {
        return RDNX_E_INVALID;
    }
    rdnx_file_t* f = (rdnx_file_t*)proc->fd_table[oldfd];
    if (!f) {
        return RDNX_E_INVALID;
    }
    rdnx_file_ref(f);
    proc->fd_table[newfd] = f;
    proc->fd_flags[newfd] = 0;   /* FD_CLOEXEC не наследуется через dup */
    return RDNX_OK;
}

static int unix_bind_fd_to_console(task_t* task, int fd, int open_flags)
{
    proc_t* proc = task_proc(task);
    if (!task || fd < 0 || fd >= PROC_MAX_FD) {
        return RDNX_E_INVALID;
    }

    if (proc->fd_table[fd]) {
        unix_fd_release(task, fd);
    }

    rdnx_file_t* f = vfs_file_open("/dev/console", open_flags);
    if (!f) {
        return RDNX_E_NOTFOUND;
    }
    f->kind = UNIX_FD_KIND_VFS;

    /* Place directly at the requested slot rather than going through
     * proc_fd_alloc() (which hands back the lowest free slot): the caller
     * asks for a specific fd number (0/1/2), and slot fd is guaranteed
     * free at this point (released above, or was never used — this only
     * runs at process bring-up). proc_fd_alloc() would happen to agree
     * today because it's called in ascending order on a fresh proc, but
     * that's an accident of call order, not something this function should
     * depend on. */
    proc->fd_table[fd] = f;
    proc->fd_flags[fd] = 0;
    return RDNX_OK;
}

int unix_bind_stdio_to_console(task_t* task)
{
    int ret = unix_bind_fd_to_console(task, 0, VFS_OPEN_READ | VFS_OPEN_WRITE);
    if (ret != RDNX_OK) {
        return ret;
    }
    ret = unix_bind_fd_to_console(task, 1, VFS_OPEN_WRITE);
    if (ret != RDNX_OK) {
        return ret;
    }
    return unix_bind_fd_to_console(task, 2, VFS_OPEN_WRITE);
}

int unix_clone_fds_for_spawn(const task_t* parent, task_t* child)
{
    if (!parent || !child) {
        return RDNX_E_INVALID;
    }
    const proc_t* pproc = task_proc(parent);
    proc_t* cproc = task_proc(child);
    if (!pproc || !cproc) {
        return RDNX_E_INVALID;
    }
    for (int fd = 0; fd < PROC_MAX_FD; fd++) {
        rdnx_file_t* f = (rdnx_file_t*)pproc->fd_table[fd];
        if (!f) {
            cproc->fd_table[fd] = NULL;
            cproc->fd_flags[fd] = 0;
            continue;
        }
        rdnx_file_ref(f);
        cproc->fd_table[fd] = f;
        cproc->fd_flags[fd] = pproc->fd_flags[fd];
    }
    return RDNX_OK;
}

void unix_apply_cloexec(task_t* task)
{
    proc_t* proc = task_proc(task);
    if (!task) {
        return;
    }
    for (int fd = 0; fd < PROC_MAX_FD; fd++) {
        if ((proc->fd_flags[fd] & UNIX_FD_CLOEXEC) == 0) {
            continue;
        }
        if (proc->fd_table[fd]) {
            unix_fd_release(task, fd);
        }
    }
}

uint64_t unix_fs_open(uint64_t user_path_ptr, uint64_t flags)
{
    int posix_flags = (int)flags;
    char path_buf[UNIX_PATH_MAX];
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int vfs_flags = unix_posix_flags_to_vfs(posix_flags);
    int open_err = RDNX_OK;
    rdnx_file_t* f = vfs_file_open_ex(path_buf, vfs_flags, &open_err);
    if (!f) {
        return (uint64_t)open_err;
    }
    /* Store only access mode + status flags; strip one-shot flags (O_CREAT, O_TRUNC). */
    ((vfs_file_t*)f->priv)->open_flags = posix_flags & ~(UNIX_O_CLOEXEC | UNIX_O_ONESHOT);
    f->kind = UNIX_FD_KIND_VFS;

    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    if (!task) {
        rdnx_file_put(f);
        return (uint64_t)RDNX_E_INVALID;
    }
    int fd = proc_fd_alloc(proc, f);
    if (fd < 0) {
        rdnx_file_put(f);
        return (uint64_t)RDNX_E_BUSY;
    }
    if (posix_flags & UNIX_O_CLOEXEC)  proc->fd_flags[fd] |= UNIX_FD_CLOEXEC;
    if (posix_flags & UNIX_O_NONBLOCK) f->status_flags |= RDNX_F_NONBLOCK;
    return (uint64_t)fd;
}

uint64_t unix_fs_open_kernel_path(const char* path, uint64_t flags)
{
    int posix_flags = (int)flags;
    if (!path || path[0] != '/') {
        return (uint64_t)RDNX_E_INVALID;
    }

    int vfs_flags = unix_posix_flags_to_vfs(posix_flags);
    int open_err = RDNX_OK;
    rdnx_file_t* f = vfs_file_open_ex(path, vfs_flags, &open_err);
    if (!f) {
        return (uint64_t)open_err;
    }
    ((vfs_file_t*)f->priv)->open_flags = posix_flags & ~(UNIX_O_CLOEXEC | UNIX_O_ONESHOT);
    f->kind = UNIX_FD_KIND_VFS;

    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    if (!task) {
        rdnx_file_put(f);
        return (uint64_t)RDNX_E_INVALID;
    }
    int fd = proc_fd_alloc(proc, f);
    if (fd < 0) {
        rdnx_file_put(f);
        return (uint64_t)RDNX_E_BUSY;
    }
    if (posix_flags & UNIX_O_CLOEXEC)  proc->fd_flags[fd] |= UNIX_FD_CLOEXEC;
    if (posix_flags & UNIX_O_NONBLOCK) f->status_flags |= RDNX_F_NONBLOCK;
    return (uint64_t)fd;
}

uint64_t unix_fs_openat(uint64_t dirfd_u, uint64_t user_path_ptr, uint64_t flags)
{
    int dirfd = (int)(int64_t)dirfd_u;
    int posix_flags = (int)flags;

    /* Read userland path first. */
    char in[UNIX_PATH_MAX];
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    if (!task || unix_copy_user_cstr(in, sizeof(in), (const char*)(uintptr_t)user_path_ptr) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }

    /* Absolute path or AT_FDCWD: same as open(). */
    if (unix_is_abs_path(in) || dirfd == UNIX_AT_FDCWD) {
        return unix_fs_open(user_path_ptr, flags);
    }

    /* Relative path + real dirfd. */
    if (dirfd < 0 || dirfd >= PROC_MAX_FD || !proc->fd_table[dirfd]) {
        return (uint64_t)RDNX_E_INVALID;
    }
    /* Only VFS fds can serve as a directory reference. */
    rdnx_file_t* dir_f = (rdnx_file_t*)proc->fd_table[dirfd];
    if (dir_f->kind != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID; /* ENOTDIR — not a VFS descriptor */
    }
    vfs_file_t* dir_file = (vfs_file_t*)dir_f->priv;
    if (!dir_file->node || dir_file->node->type != VFS_NODE_DIR) {
        return (uint64_t)RDNX_E_INVALID; /* ENOTDIR */
    }

    char dir_path[UNIX_PATH_MAX];
    if (unix_vfs_node_path(dir_file->node, dir_path, sizeof(dir_path)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }

    /* Combine dir_path + "/" + in. */
    char combined[UNIX_PATH_MAX];
    size_t p = 0;
    for (size_t i = 0; dir_path[i] && p + 1 < sizeof(combined); i++) {
        combined[p++] = dir_path[i];
    }
    if (p > 0 && combined[p - 1] != '/') {
        if (p + 1 < sizeof(combined)) combined[p++] = '/';
    }
    for (size_t i = 0; in[i] && p + 1 < sizeof(combined); i++) {
        combined[p++] = in[i];
    }
    combined[p] = '\0';

    char path_buf[UNIX_PATH_MAX];
    if (unix_path_normalize(combined, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int vfs_flags = unix_posix_flags_to_vfs(posix_flags);
    int open_err = RDNX_OK;
    rdnx_file_t* f = vfs_file_open_ex(path_buf, vfs_flags, &open_err);
    if (!f) {
        return (uint64_t)open_err;
    }
    ((vfs_file_t*)f->priv)->open_flags = posix_flags & ~(UNIX_O_CLOEXEC | UNIX_O_ONESHOT);
    f->kind = UNIX_FD_KIND_VFS;

    int fd = proc_fd_alloc(proc, f);
    if (fd < 0) {
        rdnx_file_put(f);
        return (uint64_t)RDNX_E_BUSY;
    }
    if (posix_flags & UNIX_O_CLOEXEC)  proc->fd_flags[fd] |= UNIX_FD_CLOEXEC;
    if (posix_flags & UNIX_O_NONBLOCK) f->status_flags |= RDNX_F_NONBLOCK;
    return (uint64_t)fd;
}

uint64_t unix_fs_dup(uint64_t oldfd)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    int oldi = (int)oldfd;
    if (!task || oldi < 0 || oldi >= PROC_MAX_FD || !proc->fd_table[oldi]) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int newfd = -1;
    for (int i = 0; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i]) {
            newfd = i;
            break;
        }
    }
    if (newfd < 0) {
        return (uint64_t)RDNX_E_BUSY;
    }

    int rc = unix_fd_dup_into(task, oldi, newfd);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    return (uint64_t)newfd;
}

uint64_t unix_fs_dup2(uint64_t oldfd, uint64_t newfd)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    int oldi = (int)oldfd;
    int newi = (int)newfd;
    if (!task || oldi < 0 || oldi >= PROC_MAX_FD || newi < 0 || newi >= PROC_MAX_FD || !proc->fd_table[oldi]) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (oldi == newi) {
        return (uint64_t)newi;
    }
    if (proc->fd_table[newi]) {
        unix_fd_release(task, newi);
    }
    int rc = unix_fd_dup_into(task, oldi, newi);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    return (uint64_t)newi;
}

uint64_t unix_fs_dup3(uint64_t oldfd, uint64_t newfd, uint64_t flags)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    int oldi = (int)oldfd;
    int newi = (int)newfd;
    uint32_t uflags = (uint32_t)flags;
    if (!task || oldi < 0 || oldi >= PROC_MAX_FD || newi < 0 || newi >= PROC_MAX_FD || !proc->fd_table[oldi]) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (oldi == newi) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if ((uflags & ~(UNIX_O_CLOEXEC)) != 0) {
        return (uint64_t)RDNX_E_UNSUPPORTED;
    }

    if (proc->fd_table[newi]) {
        unix_fd_release(task, newi);
    }
    int rc = unix_fd_dup_into(task, oldi, newi);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    if (uflags & UNIX_O_CLOEXEC) {
        proc->fd_flags[newi] |= UNIX_FD_CLOEXEC;
    }
    return (uint64_t)newi;
}

uint64_t unix_fs_close(uint64_t fd)
{
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!proc_fd_get(task_proc(task), (int)fd)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    unix_fd_release(task, (int)fd);
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_read(uint64_t fd, uint64_t user_buf_ptr, uint64_t len)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }

    void* buf = (void*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)len;
    if (!unix_user_range_ok(buf, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int fdi = (int)fd;
    rdnx_file_t* h = (rdnx_file_t*)proc_fd_get(proc, fdi);
    if (!h) {
        return (uint64_t)RDNX_E_INVALID;
    }

    if (h->kind == UNIX_FD_KIND_VFS) {
        return (uint64_t)h->ops->read(h, buf, n);
    }

    if (h->kind == UNIX_FD_KIND_SOCKET) {
        net_socket_t* sock = (net_socket_t*)h->priv;
        int ret = net_socket_tcp_recv(sock, buf, n, 5000u);
        return (ret >= 0) ? (uint64_t)ret : (uint64_t)RDNX_E_INVALID;
    }

    if (h->kind == UNIX_FD_KIND_PIPE_R) {
        unix_pipe_t* p = (unix_pipe_t*)h->priv;
        if (!p || p->magic != UNIX_PIPE_MAGIC) {
            return (uint64_t)RDNX_E_INVALID;
        }

        uint8_t* out = (uint8_t*)buf;
        size_t done = 0;
        while (done < n) {
            uint32_t count;
            uint32_t writers;
            uint8_t ch = 0;
            bool have_byte = false;

            irql_t old = unix_pipe_lock();
            count = p->count;
            writers = p->writers;
            if (count > 0) {
                ch = p->data[p->tail];
                p->tail = (p->tail + 1u) % UNIX_PIPE_CAP;
                p->count--;
                have_byte = true;
            }
            unix_pipe_unlock(old);

            if (have_byte) {
                out[done++] = ch;
                continue;
            }
            if (writers == 0) {
                break;
            }
            if (done > 0) {
                break;
            }
            if (h->status_flags & RDNX_F_NONBLOCK) {
                return (done > 0) ? (uint64_t)done : (uint64_t)RDNX_E_AGAIN;
            }
            scheduler_yield();
        }
        return (uint64_t)done;
    }

    return (uint64_t)RDNX_E_INVALID;
}

uint64_t unix_fs_write(uint64_t fd, uint64_t user_buf_ptr, uint64_t len)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }

    const void* buf = (const void*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)len;
    if (!unix_user_range_ok(buf, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_io_range_mapped(task, buf, n, false)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int fdi = (int)fd;
    rdnx_file_t* h = (rdnx_file_t*)proc_fd_get(proc, fdi);
    if (!h) {
        return (uint64_t)RDNX_E_INVALID;
    }

    if (h->kind == UNIX_FD_KIND_VFS) {
        return (uint64_t)h->ops->write(h, buf, n);
    }

    if (h->kind == UNIX_FD_KIND_SOCKET) {
        net_socket_t* sock = (net_socket_t*)h->priv;
        int ret = net_socket_tcp_send(sock, buf, n);
        return (ret >= 0) ? (uint64_t)ret : (uint64_t)RDNX_E_INVALID;
    }

    if (h->kind == UNIX_FD_KIND_PIPE_W) {
        unix_pipe_t* p = (unix_pipe_t*)h->priv;
        if (!p || p->magic != UNIX_PIPE_MAGIC) {
            return (uint64_t)RDNX_E_INVALID;
        }

        const uint8_t* in = (const uint8_t*)buf;
        size_t done = 0;
        while (done < n) {
            uint32_t readers;
            uint32_t count;
            bool pushed = false;

            irql_t old = unix_pipe_lock();
            readers = p->readers;
            count = p->count;
            if (readers > 0 && count < UNIX_PIPE_CAP) {
                p->data[p->head] = in[done];
                p->head = (p->head + 1u) % UNIX_PIPE_CAP;
                p->count++;
                pushed = true;
            }
            unix_pipe_unlock(old);

            if (readers == 0) {
                return (done > 0) ? (uint64_t)done : (uint64_t)RDNX_E_INVALID;
            }
            if (pushed) {
                done++;
                continue;
            }
            if (done > 0) {
                break;
            }
            if (h->status_flags & RDNX_F_NONBLOCK) {
                return (done > 0) ? (uint64_t)done : (uint64_t)RDNX_E_AGAIN;
            }
            scheduler_yield();
        }
        return (uint64_t)done;
    }

    return (uint64_t)RDNX_E_INVALID;
}

uint64_t unix_fs_lseek(uint64_t fd, uint64_t off, uint64_t whence)
{
    task_t* task = task_get_current();
    rdnx_file_t* f;

    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if ((int)fd < 0 || (int)fd >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    f = (rdnx_file_t*)proc_fd_get(task_proc(task), (int)fd);
    if (!f || f->kind != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }

    return (uint64_t)f->ops->seek(f, (int64_t)off, (int)whence);
}

uint64_t unix_fs_truncate(uint64_t user_path_ptr, uint64_t size)
{
    char path_buf[UNIX_PATH_MAX];
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_truncate(path_buf, size);
}

uint64_t unix_fs_ftruncate(uint64_t fd, uint64_t size)
{
    task_t* task = task_get_current();
    rdnx_file_t* f;
    vfs_file_t* file;

    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if ((int)fd < 0 || (int)fd >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    f = (rdnx_file_t*)proc_fd_get(task_proc(task), (int)fd);
    if (!f || f->kind != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    file = (vfs_file_t*)f->priv;
    if (!file || !file->writable) {
        return (uint64_t)RDNX_E_DENIED;
    }
    return (uint64_t)vfs_ftruncate(file, size);
}

uint64_t unix_fs_chdir(uint64_t user_path_ptr)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    char path_buf[UNIX_PATH_MAX];
    vfs_stat_t st;
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (vfs_stat(path_buf, &st) != RDNX_OK || (st.mode & 0040000u) == 0) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    strncpy(proc->cwd, path_buf, sizeof(proc->cwd) - 1);
    proc->cwd[sizeof(proc->cwd) - 1] = '\0';
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_getcwd(uint64_t user_buf_ptr, uint64_t size)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    char* out = (char*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)size;
    if (!task || !out || n == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    size_t len = strlen(proc->cwd);
    if (len + 1 > n || !unix_user_range_ok(out, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (unix_copy_to_user(out, proc->cwd, len + 1) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_mkdir(uint64_t user_path_ptr)
{
    char path_buf[UNIX_PATH_MAX];
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_mkdir(path_buf);
}

uint64_t unix_fs_unlink(uint64_t user_path_ptr)
{
    char path_buf[UNIX_PATH_MAX];
    vfs_stat_t st;
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (vfs_stat(path_buf, &st) != RDNX_OK) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    if ((st.mode & 0040000u) != 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_unlink(path_buf);
}

uint64_t unix_fs_rmdir(uint64_t user_path_ptr)
{
    char path_buf[UNIX_PATH_MAX];
    vfs_stat_t st;
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (strcmp(path_buf, "/") == 0) {
        return (uint64_t)RDNX_E_DENIED;
    }
    if (vfs_stat(path_buf, &st) != RDNX_OK) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    if ((st.mode & 0040000u) == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_unlink(path_buf);
}

uint64_t unix_fs_rename(uint64_t user_old_path_ptr, uint64_t user_new_path_ptr)
{
    char old_path[UNIX_PATH_MAX];
    char new_path[UNIX_PATH_MAX];
    if (unix_resolve_user_path((const char*)(uintptr_t)user_old_path_ptr, old_path, sizeof(old_path)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (unix_resolve_user_path((const char*)(uintptr_t)user_new_path_ptr, new_path, sizeof(new_path)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_rename(old_path, new_path);
}

uint64_t unix_fs_ioctl(uint64_t fd, uint64_t request, uint64_t user_arg_ptr)
{
    task_t* task = task_get_current();
    int fdi = (int)fd;
    if (!task || fdi < 0 || fdi >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(task), fdi);
    if (!rf || rf->kind != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)rf->priv;
    if (!file || !file->node || !file->node->inode) {
        return (uint64_t)RDNX_E_INVALID;
    }
    /* ---- Framebuffer device ioctls ---- */
    if (file->node->inode->flags & VFS_INODE_FRAMEBUFFER) {
        uint32_t disp_idx = file->node->inode->fs_aux;
        gfx_display_t* disp = gfx_display_get(disp_idx);
        if (!disp) {
            return (uint64_t)RDNX_E_INVALID;
        }
        switch ((uint32_t)request) {
            case FB_IOC_ACQUIRE: {
                int rc = gfx_fb_acquire(disp, task->task_id);
                return (uint64_t)rc;
            }
            case FB_IOC_RELEASE: {
                int rc = gfx_fb_release_for_task(disp, task->task_id);
                return (uint64_t)rc;
            }
            case FB_IOC_QUERY_OWNER: {
                if (!unix_user_range_ok((void*)(uintptr_t)user_arg_ptr, sizeof(fb_owner_info_t))) {
                    return (uint64_t)RDNX_E_INVALID;
                }
                fb_owner_info_t info;
                if (disp->fb_owner_refcount > 0) {
                    info.state         = FB_OWNER_FRAMEBUFFER_CLIENT;
                    info.owner_task_id = disp->fb_owner_task_id;
                } else {
                    info.state         = FB_OWNER_TEXT_CONSOLE;
                    info.owner_task_id = 0;
                }
                info._pad = 0;
                if (unix_copy_to_user((void*)(uintptr_t)user_arg_ptr, &info, sizeof(info)) != RDNX_OK) {
                    return (uint64_t)RDNX_E_INVALID;
                }
                return (uint64_t)RDNX_OK;
            }
            default:
                return (uint64_t)RDNX_E_UNSUPPORTED;
        }
    }

    /* ---- TTY / console ioctls ---- */
    if ((file->node->inode->flags & VFS_INODE_CONSOLE) == 0) {
        return (uint64_t)RDNX_E_UNSUPPORTED;
    }

    switch ((uint32_t)request) {
        case UNIX_TTY_IOCTL_ISATTY:
            return 1;
        case UNIX_TTY_IOCTL_GETATTR: {
            if (!unix_user_range_ok((void*)(uintptr_t)user_arg_ptr, sizeof(unix_termios_u_t))) {
                return (uint64_t)RDNX_E_INVALID;
            }
            unix_termios_u_t kterm;
            memset(&kterm, 0, sizeof(kterm));
            kterm.c_iflag = tty_console_get_iflag();
            kterm.c_oflag = tty_console_get_oflag();
            kterm.c_lflag = tty_console_get_lflag();
            for (uint32_t i = 0; i < 20; i++) {
                kterm.c_cc[i] = tty_console_get_cc(i);
            }
            if (unix_copy_to_user((void*)(uintptr_t)user_arg_ptr, &kterm, sizeof(kterm)) != RDNX_OK) {
                return (uint64_t)RDNX_E_INVALID;
            }
            return (uint64_t)RDNX_OK;
        }
        case UNIX_TTY_IOCTL_SETATTR: {
            if (!unix_user_range_ok((void*)(uintptr_t)user_arg_ptr, sizeof(unix_termios_u_t))) {
                return (uint64_t)RDNX_E_INVALID;
            }
            unix_termios_u_t kterm;
            if (unix_copy_from_user(&kterm, (const void*)(uintptr_t)user_arg_ptr, sizeof(kterm)) != RDNX_OK) {
                return (uint64_t)RDNX_E_INVALID;
            }
            tty_console_set_iflag(kterm.c_iflag);
            tty_console_set_oflag(kterm.c_oflag);
            tty_console_set_lflag(kterm.c_lflag);
            for (uint32_t i = 0; i < 20; i++) {
                tty_console_set_cc(i, kterm.c_cc[i]);
            }
            return (uint64_t)RDNX_OK;
        }
        case UNIX_TTY_IOCTL_GETWINSZ: {
            struct unix_winsize_u {
                uint16_t ws_row;
                uint16_t ws_col;
                uint16_t ws_xpixel;
                uint16_t ws_ypixel;
            } kws;
            if (!unix_user_range_ok((void*)(uintptr_t)user_arg_ptr, sizeof(kws))) {
                return (uint64_t)RDNX_E_INVALID;
            }
            tty_console_get_winsize(&kws.ws_row, &kws.ws_col,
                                    &kws.ws_xpixel, &kws.ws_ypixel);
            if (unix_copy_to_user((void*)(uintptr_t)user_arg_ptr, &kws, sizeof(kws)) != RDNX_OK) {
                return (uint64_t)RDNX_E_INVALID;
            }
            return (uint64_t)RDNX_OK;
        }
        case UNIX_TTY_IOCTL_SETWINSZ: {
            struct unix_winsize_u {
                uint16_t ws_row;
                uint16_t ws_col;
                uint16_t ws_xpixel;
                uint16_t ws_ypixel;
            } kws;
            if (!unix_user_range_ok((void*)(uintptr_t)user_arg_ptr, sizeof(kws))) {
                return (uint64_t)RDNX_E_INVALID;
            }
            if (unix_copy_from_user(&kws, (const void*)(uintptr_t)user_arg_ptr, sizeof(kws)) != RDNX_OK) {
                return (uint64_t)RDNX_E_INVALID;
            }
            tty_console_set_winsize(kws.ws_row, kws.ws_col,
                                    kws.ws_xpixel, kws.ws_ypixel);
            return (uint64_t)RDNX_OK;
        }
        default:
            return (uint64_t)RDNX_E_UNSUPPORTED;
    }
}

uint64_t unix_fs_stat(uint64_t user_path_ptr, uint64_t user_stat_ptr)
{
    unix_stat_u_t* ustat = (unix_stat_u_t*)(uintptr_t)user_stat_ptr;
    char path_buf[UNIX_PATH_MAX];
    vfs_stat_t st;
    int rc;

    if (!unix_user_range_ok(ustat, sizeof(*ustat))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }

    rc = vfs_stat(path_buf, &st);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    unix_stat_u_t kstat;
    kstat.st_mode = st.mode;
    kstat.st_uid  = st.uid;
    kstat.st_gid  = st.gid;
    kstat._pad    = 0;
    kstat.st_size = (int64_t)st.size;
    kstat.st_mtime = (int64_t)st.mtime;
    if (unix_copy_to_user((void*)(uintptr_t)user_stat_ptr, &kstat, sizeof(kstat)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_fstat(uint64_t fd, uint64_t user_stat_ptr)
{
    task_t* task = task_get_current();
    rdnx_file_t* f;
    vfs_file_t* file;
    vfs_stat_t st;
    int rc;

    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok((void*)(uintptr_t)user_stat_ptr, sizeof(unix_stat_u_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if ((int)fd < 0 || (int)fd >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    f = (rdnx_file_t*)proc_fd_get(task_proc(task), (int)fd);
    if (!f || f->kind != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    file = (vfs_file_t*)f->priv;
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rc = vfs_fstat(file, &st);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    unix_stat_u_t kstat;
    kstat.st_mode = st.mode;
    kstat.st_uid  = st.uid;
    kstat.st_gid  = st.gid;
    kstat._pad    = 0;
    kstat.st_size = (int64_t)st.size;
    kstat.st_mtime = (int64_t)st.mtime;
    if (unix_copy_to_user((void*)(uintptr_t)user_stat_ptr, &kstat, sizeof(kstat)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_chmod(uint64_t user_path_ptr, uint64_t mode)
{
    char path_buf[UNIX_PATH_MAX];
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_chmod(path_buf, (uint16_t)(mode & 0x0FFFu));
}

uint64_t unix_fs_fchmod(uint64_t fd, uint64_t mode)
{
    task_t* task = task_get_current();
    if (!task || (int)fd < 0 || (int)fd >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* f = (rdnx_file_t*)proc_fd_get(task_proc(task), (int)fd);
    if (!f || f->kind != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)f->priv;
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_fchmod(file, (uint16_t)(mode & 0x0FFFu));
}

uint64_t unix_fs_chown(uint64_t user_path_ptr, uint64_t uid, uint64_t gid)
{
    char path_buf[UNIX_PATH_MAX];
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_chown(path_buf, (uint32_t)uid, (uint32_t)gid);
}

uint64_t unix_fs_fchown(uint64_t fd, uint64_t uid, uint64_t gid)
{
    task_t* task = task_get_current();
    if (!task || (int)fd < 0 || (int)fd >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* f = (rdnx_file_t*)proc_fd_get(task_proc(task), (int)fd);
    if (!f || f->kind != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)f->priv;
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vfs_fchown(file, (uint32_t)uid, (uint32_t)gid);
}

uint64_t unix_fs_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    int fdi = (int)fd;
    int min_fd = (int)arg;
    if (!task || fdi < 0 || fdi >= PROC_MAX_FD || !proc->fd_table[fdi]) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* h = (rdnx_file_t*)proc->fd_table[fdi];

    /* Mutable status flags that F_SETFL may toggle. */
    static const int unix_setfl_mask = UNIX_O_NONBLOCK;

    switch ((int)cmd) {
        case UNIX_F_DUPFD:
        case UNIX_F_DUPFD_CLOEXEC: {
            if (min_fd < 0 || min_fd >= PROC_MAX_FD) {
                return (uint64_t)RDNX_E_INVALID;
            }
            for (int newfd = min_fd; newfd < PROC_MAX_FD; newfd++) {
                if (proc->fd_table[newfd]) {
                    continue;
                }
                int rc = unix_fd_dup_into(task, fdi, newfd);
                if (rc != RDNX_OK) {
                    return (uint64_t)rc;
                }
                if ((int)cmd == UNIX_F_DUPFD_CLOEXEC) {
                    proc->fd_flags[newfd] |= UNIX_FD_CLOEXEC;
                }
                return (uint64_t)newfd;
            }
            return (uint64_t)RDNX_E_BUSY;
        }
        case UNIX_F_GETFD:
            return (uint64_t)(proc->fd_flags[fdi] & UNIX_FD_CLOEXEC);
        case UNIX_F_SETFD:
            proc->fd_flags[fdi] = ((uint8_t)arg) & UNIX_FD_CLOEXEC;
            return (uint64_t)RDNX_OK;
        case UNIX_F_GETFL: {
            uint8_t kind = h->kind;
            if (kind == UNIX_FD_KIND_VFS) {
                vfs_file_t* f = (vfs_file_t*)h->priv;
                if (!f) return (uint64_t)RDNX_E_INVALID;
                return (uint64_t)f->open_flags;
            }
            /* Pipe / socket: synthesise flags from fd_kind + fd_flags. */
            int fl = 0;
            if (kind == UNIX_FD_KIND_PIPE_R) {
                fl = UNIX_O_RDONLY;
            } else if (kind == UNIX_FD_KIND_PIPE_W) {
                fl = UNIX_O_WRONLY;
            }
            if (h->status_flags & RDNX_F_NONBLOCK) {
                fl |= UNIX_O_NONBLOCK;
            }
            return (uint64_t)fl;
        }
        case UNIX_F_SETFL: {
            uint8_t kind = h->kind;
            if (kind == UNIX_FD_KIND_VFS) {
                vfs_file_t* f = (vfs_file_t*)h->priv;
                if (!f) return (uint64_t)RDNX_E_INVALID;
                f->open_flags = (f->open_flags & ~unix_setfl_mask) |
                                ((int)arg & unix_setfl_mask);
            }
            /* For any fd kind, track O_NONBLOCK on the shared description —
             * not fd_flags[], which is per-descriptor and would let a dup()'d
             * fd silently disagree with the original about O_NONBLOCK. */
            if ((int)arg & UNIX_O_NONBLOCK) {
                h->status_flags |= RDNX_F_NONBLOCK;
            } else {
                h->status_flags &= ~RDNX_F_NONBLOCK;
            }
            return (uint64_t)RDNX_OK;
        }
        default:
            return (uint64_t)RDNX_E_UNSUPPORTED;
    }
}

static int unix_poll_one(task_t* task, unix_pollfd_u_t* pfd)
{
    proc_t* proc = task_proc(task);
    int16_t rev = 0;
    if (!task || !pfd) {
        return 0;
    }

    int fdi = pfd->fd;
    if (fdi < 0 || fdi >= PROC_MAX_FD || !proc->fd_table[fdi]) {
        pfd->revents = UNIX_POLLNVAL;
        return 1;
    }
    rdnx_file_t* h = (rdnx_file_t*)proc->fd_table[fdi];

    uint8_t kind = h->kind;
    if (kind == UNIX_FD_KIND_VFS) {
        vfs_file_t* file = (vfs_file_t*)h->priv;
        if (!file || !file->node || !file->node->inode) {
            pfd->revents = UNIX_POLLNVAL;
            return 1;
        }
        vfs_inode_t* inode = file->node->inode;
        if (inode->flags & VFS_INODE_CONSOLE) {
            if ((pfd->events & UNIX_POLLIN) && tty_console_poll_readable()) {
                rev |= UNIX_POLLIN;
            }
            if (pfd->events & UNIX_POLLOUT) {
                rev |= UNIX_POLLOUT;
            }
        } else {
            if (pfd->events & UNIX_POLLIN) {
                rev |= UNIX_POLLIN;
            }
            if ((pfd->events & UNIX_POLLOUT) && file->writable) {
                rev |= UNIX_POLLOUT;
            }
        }
    } else if (kind == UNIX_FD_KIND_PIPE_R) {
        unix_pipe_t* p = (unix_pipe_t*)h->priv;
        if (!p || p->magic != UNIX_PIPE_MAGIC) {
            pfd->revents = UNIX_POLLNVAL;
            return 1;
        }
        irql_t old = unix_pipe_lock();
        uint32_t count = p->count;
        uint32_t writers = p->writers;
        unix_pipe_unlock(old);

        if ((pfd->events & UNIX_POLLIN) && (count > 0 || writers == 0)) {
            rev |= UNIX_POLLIN;
        }
        if (writers == 0) {
            rev |= UNIX_POLLHUP;
        }
    } else if (kind == UNIX_FD_KIND_PIPE_W) {
        unix_pipe_t* p = (unix_pipe_t*)h->priv;
        if (!p || p->magic != UNIX_PIPE_MAGIC) {
            pfd->revents = UNIX_POLLNVAL;
            return 1;
        }
        irql_t old = unix_pipe_lock();
        uint32_t count = p->count;
        uint32_t readers = p->readers;
        unix_pipe_unlock(old);

        if ((pfd->events & UNIX_POLLOUT) && readers > 0 && count < UNIX_PIPE_CAP) {
            rev |= UNIX_POLLOUT;
        }
        if (readers == 0) {
            rev |= UNIX_POLLHUP | UNIX_POLLERR;
        }
    } else {
        rev |= UNIX_POLLNVAL;
    }

    pfd->revents = rev;
    return (rev != 0) ? 1 : 0;
}

uint64_t unix_fs_poll(uint64_t user_fds_ptr, uint64_t nfds, int64_t timeout_ms)
{
    task_t* task = task_get_current();
    unix_pollfd_u_t* pfds = (unix_pollfd_u_t*)(uintptr_t)user_fds_ptr;
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (nfds > PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (nfds > 0 && (!pfds || !unix_user_range_ok(pfds, (size_t)nfds * sizeof(*pfds)))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint64_t deadline = 0;
    if (timeout_ms >= 0) {
        uint64_t now = scheduler_get_ticks();
        uint64_t ticks = ((uint64_t)timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1u)) / SCHEDULER_TIME_SLICE_MS;
        deadline = now + ticks;
    }

    for (;;) {
        int ready = 0;
        for (uint64_t i = 0; i < nfds; i++) {
            ready += unix_poll_one(task, &pfds[i]);
        }
        if (ready > 0) {
            return (uint64_t)ready;
        }
        if (timeout_ms == 0) {
            return 0;
        }
        if (timeout_ms > 0 && scheduler_get_ticks() >= deadline) {
            return 0;
        }
        scheduler_yield();
    }
}

uint64_t unix_fs_select(uint64_t nfds,
                        uint64_t user_readfds_ptr,
                        uint64_t user_writefds_ptr,
                        uint64_t user_exceptfds_ptr,
                        uint64_t user_timeout_ptr)
{
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (nfds > PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }

    const unix_fdset_u_t* user_r = (const unix_fdset_u_t*)(uintptr_t)user_readfds_ptr;
    const unix_fdset_u_t* user_w = (const unix_fdset_u_t*)(uintptr_t)user_writefds_ptr;
    const unix_fdset_u_t* user_e = (const unix_fdset_u_t*)(uintptr_t)user_exceptfds_ptr;

    unix_fdset_u_t in_r = {0}, in_w = {0}, in_e = {0};
    unix_fdset_u_t out_r = {0}, out_w = {0}, out_e = {0};

    if (user_r) {
        if (!unix_user_range_ok(user_r, sizeof(*user_r))) {
            return (uint64_t)RDNX_E_INVALID;
        }
        in_r = *user_r;
    }
    if (user_w) {
        if (!unix_user_range_ok(user_w, sizeof(*user_w))) {
            return (uint64_t)RDNX_E_INVALID;
        }
        in_w = *user_w;
    }
    if (user_e) {
        if (!unix_user_range_ok(user_e, sizeof(*user_e))) {
            return (uint64_t)RDNX_E_INVALID;
        }
        in_e = *user_e;
    }

    int64_t timeout_ms = -1;
    if (user_timeout_ptr != 0) {
        const unix_timeval_u_t* tv = (const unix_timeval_u_t*)(uintptr_t)user_timeout_ptr;
        if (!unix_user_range_ok(tv, sizeof(*tv))) {
            return (uint64_t)RDNX_E_INVALID;
        }
        if (tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec >= 1000000) {
            return (uint64_t)RDNX_E_INVALID;
        }
        uint64_t total_ms = (uint64_t)tv->tv_sec * 1000ULL + (uint64_t)((tv->tv_usec + 999) / 1000);
        timeout_ms = (int64_t)total_ms;
    }

    uint64_t deadline = 0;
    if (timeout_ms >= 0) {
        uint64_t now = scheduler_get_ticks();
        uint64_t ticks = ((uint64_t)timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1u)) / SCHEDULER_TIME_SLICE_MS;
        deadline = now + ticks;
    }

    for (;;) {
        out_r.bits = 0;
        out_w.bits = 0;
        out_e.bits = 0;
        int ready = 0;

        for (uint64_t fd = 0; fd < nfds; fd++) {
            int want_r = unix_fdset_test(&in_r, (uint32_t)fd);
            int want_w = unix_fdset_test(&in_w, (uint32_t)fd);
            int want_e = unix_fdset_test(&in_e, (uint32_t)fd);
            if (!want_r && !want_w && !want_e) {
                continue;
            }

            unix_pollfd_u_t pfd;
            pfd.fd = (int32_t)fd;
            pfd.events = 0;
            pfd.revents = 0;
            if (want_r) {
                pfd.events |= UNIX_POLLIN;
            }
            if (want_w) {
                pfd.events |= UNIX_POLLOUT;
            }
            if (want_e) {
                pfd.events |= UNIX_POLLERR;
            }

            (void)unix_poll_one(task, &pfd);
            int fd_ready = 0;
            if (want_r && (pfd.revents & (UNIX_POLLIN | UNIX_POLLHUP))) {
                unix_fdset_set(&out_r, (uint32_t)fd);
                fd_ready = 1;
            }
            if (want_w && (pfd.revents & UNIX_POLLOUT)) {
                unix_fdset_set(&out_w, (uint32_t)fd);
                fd_ready = 1;
            }
            if (want_e && (pfd.revents & (UNIX_POLLERR | UNIX_POLLNVAL))) {
                unix_fdset_set(&out_e, (uint32_t)fd);
                fd_ready = 1;
            }
            if (fd_ready) {
                ready++;
            }
        }

        if (user_r) {
            * (unix_fdset_u_t*) user_r = out_r;
        }
        if (user_w) {
            * (unix_fdset_u_t*) user_w = out_w;
        }
        if (user_e) {
            * (unix_fdset_u_t*) user_e = out_e;
        }

        if (ready > 0) {
            return (uint64_t)ready;
        }
        if (timeout_ms == 0) {
            return 0;
        }
        if (timeout_ms > 0 && scheduler_get_ticks() >= deadline) {
            return 0;
        }
        scheduler_yield();
    }
}

/* Allocate a pipe and two fds for it in kernel space.
 * Returns RDNX_OK on success; fd numbers are written to fd_r_out/fd_w_out.
 * On failure the pipe object and any allocated fds are cleaned up. */
static int unix_pipe_alloc_fds(task_t* task, int* fd_r_out, int* fd_w_out)
{
    proc_t* proc = task_proc(task);
    unix_pipe_t* p = (unix_pipe_t*)kmalloc(sizeof(unix_pipe_t));
    if (!p) {
        return RDNX_E_NOMEM;
    }
    p->magic   = UNIX_PIPE_MAGIC;
    p->readers = 1;
    p->writers = 1;
    p->head    = 0;
    p->tail    = 0;
    p->count   = 0;

    rdnx_file_t* fr = rdnx_file_alloc(&pipe_fileops_stub, p);
    if (!fr) {
        p->magic = 0;
        kfree(p);
        return RDNX_E_NOMEM;
    }
    fr->kind = UNIX_FD_KIND_PIPE_R;
    int fd_r = proc_fd_alloc(proc, fr);
    if (fd_r < 0) {
        /* fr was never installed into the fd table; nothing else references p. */
        kfree(fr);
        p->magic = 0;
        kfree(p);
        return RDNX_E_BUSY;
    }

    rdnx_file_t* fw = rdnx_file_alloc(&pipe_fileops_stub, p);
    if (!fw) {
        /* No write-end wrapper was ever created, so nothing will ever
         * decrement p->writers. Force it to 0 here so releasing fd_r drops
         * p->readers to 0 too and unix_pipe_release() actually frees p —
         * otherwise p leaks forever (readers==0 but writers stuck at 1). */
        irql_t old = unix_pipe_lock();
        p->writers = 0;
        unix_pipe_unlock(old);
        unix_fd_release(task, fd_r);
        return RDNX_E_NOMEM;
    }
    fw->kind = UNIX_FD_KIND_PIPE_W;
    int fd_w = proc_fd_alloc(proc, fw);
    if (fd_w < 0) {
        unix_fd_release(task, fd_r);
        rdnx_file_put(fw);
        return RDNX_E_BUSY;
    }

    *fd_r_out = fd_r;
    *fd_w_out = fd_w;
    return RDNX_OK;
}

uint64_t unix_fs_pipe(uint64_t user_pipefd_ptr)
{
    int* out = (int*)(uintptr_t)user_pipefd_ptr;
    task_t* task = task_get_current();
    if (!task || !unix_user_range_ok(out, sizeof(int) * 2u)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int fd_r, fd_w;
    int rc = unix_pipe_alloc_fds(task, &fd_r, &fd_w);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }

    int fds[2] = { fd_r, fd_w };
    if (unix_copy_to_user(out, fds, sizeof(fds)) != RDNX_OK) {
        unix_fd_release(task, fd_r);
        unix_fd_release(task, fd_w);
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_pipe2(uint64_t user_pipefd_ptr, uint64_t flags)
{
    uint32_t uflags = (uint32_t)flags;
    if ((uflags & ~(UNIX_O_CLOEXEC | UNIX_O_NONBLOCK)) != 0) {
        return (uint64_t)RDNX_E_UNSUPPORTED;
    }

    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    void* out = (void*)(uintptr_t)user_pipefd_ptr;
    if (!task || !out || !unix_user_range_ok(out, sizeof(int) * 2u)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    /* Allocate pipe and get fd numbers kernel-side; never re-read from user. */
    int fd_r, fd_w;
    int rc = unix_pipe_alloc_fds(task, &fd_r, &fd_w);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }

    if (uflags & UNIX_O_CLOEXEC) {
        proc->fd_flags[fd_r] |= UNIX_FD_CLOEXEC;
        proc->fd_flags[fd_w] |= UNIX_FD_CLOEXEC;
    }
    if (uflags & UNIX_O_NONBLOCK) {
        /* O_NONBLOCK lives on the shared description, not fd_flags[]. */
        rdnx_file_t* fr = (rdnx_file_t*)proc->fd_table[fd_r];
        rdnx_file_t* fw = (rdnx_file_t*)proc->fd_table[fd_w];
        if (fr) fr->status_flags |= RDNX_F_NONBLOCK;
        if (fw) fw->status_flags |= RDNX_F_NONBLOCK;
    }

    int fds[2] = { fd_r, fd_w };
    if (unix_copy_to_user(out, fds, sizeof(fds)) != RDNX_OK) {
        unix_fd_release(task, fd_r);
        unix_fd_release(task, fd_w);
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_socket(uint64_t domain, uint64_t type, uint64_t protocol)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }

    net_socket_t* sock = net_socket_create((int)domain, (int)type, (int)protocol);
    if (!sock) {
        return (uint64_t)RDNX_E_INVALID;
    }

    rdnx_file_t* f = rdnx_file_alloc(&socket_fileops_stub, sock);
    if (!f) {
        net_socket_close(sock);
        return (uint64_t)RDNX_E_NOMEM;
    }
    f->kind = UNIX_FD_KIND_SOCKET;

    int fd = proc_fd_alloc(proc, f);
    if (fd < 0) {
        rdnx_file_put(f);
        return (uint64_t)RDNX_E_BUSY;
    }
    proc->fd_flags[fd] = 0;
    return (uint64_t)fd;
}

uint64_t unix_fs_bind(uint64_t fd, uint64_t user_addr_ptr)
{
    task_t* task = task_get_current();
    int fdi = (int)fd;
    sockaddr_in_t* addr = (sockaddr_in_t*)(uintptr_t)user_addr_ptr;
    if (!task || fdi < 0 || fdi >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(task), fdi);
    if (!rf || rf->kind != UNIX_FD_KIND_SOCKET) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!addr || !unix_user_range_ok(addr, sizeof(*addr))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    net_socket_t* sock = (net_socket_t*)rf->priv;
    if (!sock) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (net_socket_bind(sock, addr) == 0) ? (uint64_t)RDNX_OK : (uint64_t)RDNX_E_INVALID;
}

uint64_t unix_fs_connect(uint64_t fd, uint64_t user_addr_ptr)
{
    task_t* task = task_get_current();
    int fdi = (int)fd;
    sockaddr_in_t* addr = (sockaddr_in_t*)(uintptr_t)user_addr_ptr;
    if (!task || fdi < 0 || fdi >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(task), fdi);
    if (!rf || rf->kind != UNIX_FD_KIND_SOCKET) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!addr || !unix_user_range_ok(addr, sizeof(*addr))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    net_socket_t* sock = (net_socket_t*)rf->priv;
    if (!sock) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (net_socket_connect(sock, addr) == 0) ? (uint64_t)RDNX_OK : (uint64_t)RDNX_E_INVALID;
}

uint64_t unix_fs_sendto(uint64_t fd,
                        uint64_t user_buf_ptr,
                        uint64_t len,
                        uint64_t flags,
                        uint64_t user_dst_addr_ptr,
                        uint64_t user_dst_len)
{
    (void)flags;
    task_t* task = task_get_current();
    int fdi = (int)fd;
    const void* buf = (const void*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)len;
    sockaddr_in_t* dst = (sockaddr_in_t*)(uintptr_t)user_dst_addr_ptr;
    if (!task || fdi < 0 || fdi >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(task), fdi);
    if (!rf || rf->kind != UNIX_FD_KIND_SOCKET) {
        return (uint64_t)RDNX_E_INVALID;
    }
    /* n == 0 is a valid zero-length datagram per POSIX; only validate the
     * buffer pointer and mapping when there is actually data to send. */
    if (n > 0 && (!buf || !unix_user_range_ok(buf, n))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (n > 0 && !unix_user_io_range_mapped(task, buf, n, false)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!dst || user_dst_len < sizeof(sockaddr_in_t) || !unix_user_range_ok(dst, sizeof(*dst))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    net_socket_t* sock = (net_socket_t*)rf->priv;
    if (!sock) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int rc = net_socket_sendto(sock, buf, n, dst);
    if (rc < 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)rc;
}

uint64_t unix_fs_recvfrom(uint64_t fd,
                          uint64_t user_buf_ptr,
                          uint64_t len,
                          uint64_t flags,
                          uint64_t user_src_addr_ptr,
                          uint64_t timeout_ms)
{
    (void)flags;
    task_t* task = task_get_current();
    int fdi = (int)fd;
    void* buf = (void*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)len;
    sockaddr_in_t* src = (sockaddr_in_t*)(uintptr_t)user_src_addr_ptr;
    if (!task || fdi < 0 || fdi >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(task), fdi);
    if (!rf || rf->kind != UNIX_FD_KIND_SOCKET) {
        return (uint64_t)RDNX_E_INVALID;
    }
    /* n == 0 is a valid zero-length datagram per POSIX; only validate the
     * buffer pointer and mapping when there is actually data to receive. */
    if (n > 0 && (!buf || !unix_user_range_ok(buf, n))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (n > 0 && !unix_user_io_range_mapped(task, buf, n, true)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (src && !unix_user_range_ok(src, sizeof(*src))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    net_socket_t* sock = (net_socket_t*)rf->priv;
    if (!sock) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int rc = net_socket_recvfrom(sock, buf, n, src, timeout_ms);
    if (rc < 0) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    return (uint64_t)rc;
}

uint64_t unix_fs_listen(uint64_t fd, uint64_t backlog)
{
    task_t* task = task_get_current();
    int fdi = (int)fd;
    if (!task || fdi < 0 || fdi >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(task), fdi);
    if (!rf || rf->kind != UNIX_FD_KIND_SOCKET) {
        return (uint64_t)RDNX_E_INVALID;
    }
    net_socket_t* sock = (net_socket_t*)rf->priv;
    if (!sock) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (net_socket_listen(sock, (int)backlog) == 0)
           ? (uint64_t)RDNX_OK
           : (uint64_t)RDNX_E_INVALID;
}

uint64_t unix_fs_accept(uint64_t fd, uint64_t user_addr_ptr,
                        uint64_t timeout_ms)
{
    task_t* task = task_get_current();
    proc_t* proc = task_proc(task);
    int fdi = (int)fd;
    if (!task || fdi < 0 || fdi >= PROC_MAX_FD) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(proc, fdi);
    if (!rf || rf->kind != UNIX_FD_KIND_SOCKET) {
        return (uint64_t)RDNX_E_INVALID;
    }
    sockaddr_in_t* addr = (sockaddr_in_t*)(uintptr_t)user_addr_ptr;
    if (addr && !unix_user_range_ok(addr, sizeof(*addr))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    net_socket_t* listen_sock = (net_socket_t*)rf->priv;
    if (!listen_sock) {
        return (uint64_t)RDNX_E_INVALID;
    }

    net_socket_t* conn_sock = net_socket_accept(listen_sock, addr, timeout_ms);
    if (!conn_sock) {
        return (uint64_t)RDNX_E_TIMEOUT;
    }

    rdnx_file_t* nf = rdnx_file_alloc(&socket_fileops_stub, conn_sock);
    if (!nf) {
        net_socket_close(conn_sock);
        return (uint64_t)RDNX_E_NOMEM;
    }
    nf->kind = UNIX_FD_KIND_SOCKET;

    int new_fd = proc_fd_alloc(proc, nf);
    if (new_fd < 0) {
        rdnx_file_put(nf);
        return (uint64_t)RDNX_E_BUSY;
    }
    proc->fd_flags[new_fd] = 0;
    return (uint64_t)new_fd;
}
