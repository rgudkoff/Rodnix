#include "../unix_layer.h"
#include "../../fs/vfs.h"
#include "../../common/heap.h"
#include "../../common/scheduler.h"
#include "../../core/interrupts.h"
#include "../../vm/vm_map.h"
#include "../../../include/common.h"
#include "../../../include/error.h"

enum {
    UNIX_F_GETFD = 1,
    UNIX_F_SETFD = 2,
    UNIX_FD_CLOEXEC = 1,
    UNIX_PIPE_CAP = 4096
};

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
    char tmp[UNIX_PATH_MAX];
    size_t p = 0;

    if (!task || !in || !out || out_sz < 2) {
        return RDNX_E_INVALID;
    }

    if (unix_is_abs_path(in)) {
        return unix_path_normalize(in, out, out_sz);
    }

    for (size_t i = 0; task->cwd[i] != '\0' && p + 1 < sizeof(tmp); i++) {
        tmp[p++] = task->cwd[i];
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

static void unix_pipe_retain(unix_pipe_t* p, uint8_t kind)
{
    if (!p || p->magic != UNIX_PIPE_MAGIC) {
        return;
    }
    if (kind == UNIX_FD_KIND_PIPE_R) {
        p->readers++;
    } else if (kind == UNIX_FD_KIND_PIPE_W) {
        p->writers++;
    }
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

void unix_fd_release(task_t* task, int fd)
{
    if (!task || fd < 0 || fd >= TASK_MAX_FD || !task->fd_table[fd]) {
        return;
    }

    uint8_t kind = task->fd_kind[fd];
    if (kind == UNIX_FD_KIND_VFS) {
        vfs_file_t* file = (vfs_file_t*)task->fd_table[fd];
        vfs_close(file);
        kfree(file);
    } else if (kind == UNIX_FD_KIND_PIPE_R || kind == UNIX_FD_KIND_PIPE_W) {
        unix_pipe_t* p = (unix_pipe_t*)task->fd_table[fd];
        unix_pipe_release(p, kind);
    }

    (void)task_fd_close(task, fd);
}

static int unix_fd_dup_into(task_t* task, int oldfd, int newfd)
{
    if (!task || oldfd < 0 || oldfd >= TASK_MAX_FD || newfd < 0 || newfd >= TASK_MAX_FD) {
        return RDNX_E_INVALID;
    }
    if (!task->fd_table[oldfd]) {
        return RDNX_E_INVALID;
    }

    uint8_t kind = task->fd_kind[oldfd];
    if (kind == UNIX_FD_KIND_VFS) {
        vfs_file_t* copy = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
        if (!copy) {
            return RDNX_E_NOMEM;
        }
        *copy = *(vfs_file_t*)task->fd_table[oldfd];
        task->fd_table[newfd] = copy;
        task->fd_kind[newfd] = UNIX_FD_KIND_VFS;
        task->fd_flags[newfd] = 0;
        return RDNX_OK;
    }
    if (kind == UNIX_FD_KIND_PIPE_R || kind == UNIX_FD_KIND_PIPE_W) {
        unix_pipe_t* p = (unix_pipe_t*)task->fd_table[oldfd];
        if (!p || p->magic != UNIX_PIPE_MAGIC) {
            return RDNX_E_INVALID;
        }
        irql_t old = unix_pipe_lock();
        unix_pipe_retain(p, kind);
        unix_pipe_unlock(old);
        task->fd_table[newfd] = p;
        task->fd_kind[newfd] = kind;
        task->fd_flags[newfd] = 0;
        return RDNX_OK;
    }
    return RDNX_E_UNSUPPORTED;
}

static int unix_bind_fd_to_console(task_t* task, int fd, int open_flags)
{
    if (!task || fd < 0 || fd >= TASK_MAX_FD) {
        return RDNX_E_INVALID;
    }

    if (task->fd_table[fd]) {
        unix_fd_release(task, fd);
    }

    vfs_file_t* file = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!file) {
        return RDNX_E_NOMEM;
    }
    if (vfs_open("/dev/console", open_flags, file) != RDNX_OK) {
        kfree(file);
        return RDNX_E_NOTFOUND;
    }

    task->fd_table[fd] = file;
    task->fd_kind[fd] = UNIX_FD_KIND_VFS;
    task->fd_flags[fd] = 0;
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
    for (int fd = 0; fd < TASK_MAX_FD; fd++) {
        void* src = parent->fd_table[fd];
        uint8_t kind = parent->fd_kind[fd];
        if (!src) {
            child->fd_table[fd] = NULL;
            child->fd_flags[fd] = 0;
            child->fd_kind[fd] = UNIX_FD_KIND_NONE;
            continue;
        }

        if (kind == UNIX_FD_KIND_VFS) {
            vfs_file_t* copy = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
            if (!copy) {
                for (int i = 0; i < TASK_MAX_FD; i++) {
                    if (child->fd_table[i]) {
                        unix_fd_release(child, i);
                    }
                }
                return RDNX_E_NOMEM;
            }
            *copy = *(vfs_file_t*)src;
            child->fd_table[fd] = copy;
            child->fd_kind[fd] = UNIX_FD_KIND_VFS;
            child->fd_flags[fd] = parent->fd_flags[fd];
            continue;
        }

        if (kind == UNIX_FD_KIND_PIPE_R || kind == UNIX_FD_KIND_PIPE_W) {
            unix_pipe_t* p = (unix_pipe_t*)src;
            if (!p || p->magic != UNIX_PIPE_MAGIC) {
                for (int i = 0; i < TASK_MAX_FD; i++) {
                    if (child->fd_table[i]) {
                        unix_fd_release(child, i);
                    }
                }
                return RDNX_E_INVALID;
            }
            irql_t old = unix_pipe_lock();
            unix_pipe_retain(p, kind);
            unix_pipe_unlock(old);
            child->fd_table[fd] = p;
            child->fd_kind[fd] = kind;
            child->fd_flags[fd] = parent->fd_flags[fd];
            continue;
        }

        for (int i = 0; i < TASK_MAX_FD; i++) {
            if (child->fd_table[i]) {
                unix_fd_release(child, i);
            }
        }
        return RDNX_E_UNSUPPORTED;
    }
    return RDNX_OK;
}

void unix_apply_cloexec(task_t* task)
{
    if (!task) {
        return;
    }
    for (int fd = 0; fd < TASK_MAX_FD; fd++) {
        if ((task->fd_flags[fd] & UNIX_FD_CLOEXEC) == 0) {
            continue;
        }
        if (task->fd_table[fd]) {
            unix_fd_release(task, fd);
        }
    }
}

uint64_t unix_fs_open(uint64_t user_path_ptr, uint64_t flags)
{
    char path_buf[UNIX_PATH_MAX];
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!file) {
        return (uint64_t)RDNX_E_NOMEM;
    }
    if (vfs_open(path_buf, (int)flags, file) != RDNX_OK) {
        kfree(file);
        return (uint64_t)RDNX_E_NOTFOUND;
    }

    task_t* task = task_get_current();
    if (!task) {
        vfs_close(file);
        kfree(file);
        return (uint64_t)RDNX_E_INVALID;
    }
    int fd = task_fd_alloc(task, file);
    if (fd < 0) {
        vfs_close(file);
        kfree(file);
        return (uint64_t)RDNX_E_BUSY;
    }
    task->fd_kind[fd] = UNIX_FD_KIND_VFS;
    return (uint64_t)fd;
}

uint64_t unix_fs_dup(uint64_t oldfd)
{
    task_t* task = task_get_current();
    int oldi = (int)oldfd;
    if (!task || oldi < 0 || oldi >= TASK_MAX_FD || !task->fd_table[oldi]) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int newfd = -1;
    for (int i = 0; i < TASK_MAX_FD; i++) {
        if (!task->fd_table[i]) {
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
    int oldi = (int)oldfd;
    int newi = (int)newfd;
    if (!task || oldi < 0 || oldi >= TASK_MAX_FD || newi < 0 || newi >= TASK_MAX_FD || !task->fd_table[oldi]) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (oldi == newi) {
        return (uint64_t)newi;
    }
    if (task->fd_table[newi]) {
        unix_fd_release(task, newi);
    }
    int rc = unix_fd_dup_into(task, oldi, newi);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    return (uint64_t)newi;
}

uint64_t unix_fs_close(uint64_t fd)
{
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!task_fd_get(task, (int)fd)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    unix_fd_release(task, (int)fd);
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_read(uint64_t fd, uint64_t user_buf_ptr, uint64_t len)
{
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }

    void* buf = (void*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)len;
    if (!unix_user_range_ok(buf, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    int fdi = (int)fd;
    void* h = task_fd_get(task, fdi);
    if (!h) {
        return (uint64_t)RDNX_E_INVALID;
    }

    if (task->fd_kind[fdi] == UNIX_FD_KIND_VFS) {
        vfs_file_t* file = (vfs_file_t*)h;
        int ret = vfs_read(file, buf, n);
        return (ret < 0) ? (uint64_t)RDNX_E_GENERIC : (uint64_t)ret;
    }

    if (task->fd_kind[fdi] == UNIX_FD_KIND_PIPE_R) {
        unix_pipe_t* p = (unix_pipe_t*)h;
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
            scheduler_yield();
        }
        return (uint64_t)done;
    }

    return (uint64_t)RDNX_E_INVALID;
}

uint64_t unix_fs_write(uint64_t fd, uint64_t user_buf_ptr, uint64_t len)
{
    task_t* task = task_get_current();
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
    void* h = task_fd_get(task, fdi);
    if (!h) {
        return (uint64_t)RDNX_E_INVALID;
    }

    if (task->fd_kind[fdi] == UNIX_FD_KIND_VFS) {
        vfs_file_t* file = (vfs_file_t*)h;
        int ret = vfs_write(file, buf, n);
        return (ret < 0) ? (uint64_t)RDNX_E_GENERIC : (uint64_t)ret;
    }

    if (task->fd_kind[fdi] == UNIX_FD_KIND_PIPE_W) {
        unix_pipe_t* p = (unix_pipe_t*)h;
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
            scheduler_yield();
        }
        return (uint64_t)done;
    }

    return (uint64_t)RDNX_E_INVALID;
}

uint64_t unix_fs_lseek(uint64_t fd, uint64_t off, uint64_t whence)
{
    task_t* task = task_get_current();
    vfs_file_t* file;
    uint64_t new_pos = 0;
    int ret;

    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if ((int)fd < 0 || (int)fd >= TASK_MAX_FD || task->fd_kind[(int)fd] != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    file = (vfs_file_t*)task_fd_get(task, (int)fd);
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }

    ret = vfs_seek(file, (int64_t)off, (int)whence, &new_pos);
    if (ret != RDNX_OK) {
        return (uint64_t)ret;
    }
    return new_pos;
}

uint64_t unix_fs_chdir(uint64_t user_path_ptr)
{
    task_t* task = task_get_current();
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
    strncpy(task->cwd, path_buf, sizeof(task->cwd) - 1);
    task->cwd[sizeof(task->cwd) - 1] = '\0';
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_getcwd(uint64_t user_buf_ptr, uint64_t size)
{
    task_t* task = task_get_current();
    char* out = (char*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)size;
    if (!task || !out || n == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    size_t len = strlen(task->cwd);
    if (len + 1 > n || !unix_user_range_ok(out, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    memcpy(out, task->cwd, len + 1);
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
    ustat->st_mode = st.mode;
    ustat->st_size = (int64_t)st.size;
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_fstat(uint64_t fd, uint64_t user_stat_ptr)
{
    task_t* task = task_get_current();
    vfs_file_t* file;
    unix_stat_u_t* ustat = (unix_stat_u_t*)(uintptr_t)user_stat_ptr;
    vfs_stat_t st;
    int rc;

    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(ustat, sizeof(*ustat))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if ((int)fd < 0 || (int)fd >= TASK_MAX_FD || task->fd_kind[(int)fd] != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    file = (vfs_file_t*)task_fd_get(task, (int)fd);
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    rc = vfs_fstat(file, &st);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    ustat->st_mode = st.mode;
    ustat->st_size = (int64_t)st.size;
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg)
{
    task_t* task = task_get_current();
    int fdi = (int)fd;
    if (!task || fdi < 0 || fdi >= TASK_MAX_FD || !task->fd_table[fdi]) {
        return (uint64_t)RDNX_E_INVALID;
    }

    switch ((int)cmd) {
        case UNIX_F_GETFD:
            return (uint64_t)(task->fd_flags[fdi] & UNIX_FD_CLOEXEC);
        case UNIX_F_SETFD:
            task->fd_flags[fdi] = ((uint8_t)arg) & UNIX_FD_CLOEXEC;
            return (uint64_t)RDNX_OK;
        default:
            return (uint64_t)RDNX_E_UNSUPPORTED;
    }
}

uint64_t unix_fs_pipe(uint64_t user_pipefd_ptr)
{
    int* out = (int*)(uintptr_t)user_pipefd_ptr;
    task_t* task = task_get_current();
    if (!task || !unix_user_range_ok(out, sizeof(int) * 2u)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    unix_pipe_t* p = (unix_pipe_t*)kmalloc(sizeof(unix_pipe_t));
    if (!p) {
        return (uint64_t)RDNX_E_NOMEM;
    }
    p->magic = UNIX_PIPE_MAGIC;
    p->readers = 1;
    p->writers = 1;
    p->head = 0;
    p->tail = 0;
    p->count = 0;

    int fd_r = task_fd_alloc(task, p);
    if (fd_r < 0) {
        p->magic = 0;
        kfree(p);
        return (uint64_t)RDNX_E_BUSY;
    }
    task->fd_kind[fd_r] = UNIX_FD_KIND_PIPE_R;

    int fd_w = task_fd_alloc(task, p);
    if (fd_w < 0) {
        unix_fd_release(task, fd_r);
        return (uint64_t)RDNX_E_BUSY;
    }
    task->fd_kind[fd_w] = UNIX_FD_KIND_PIPE_W;

    out[0] = fd_r;
    out[1] = fd_w;
    return (uint64_t)RDNX_OK;
}
