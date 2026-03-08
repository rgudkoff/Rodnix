#include "../unix_layer.h"
#include "../../fs/vfs.h"
#include "../../common/heap.h"
#include "../../vm/vm_map.h"
#include "../../../include/error.h"

enum {
    UNIX_F_GETFD = 1,
    UNIX_F_SETFD = 2,
    UNIX_FD_CLOEXEC = 1
};

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

static int unix_bind_fd_to_console(task_t* task, int fd, int open_flags)
{
    if (!task || fd < 0 || fd >= TASK_MAX_FD) {
        return RDNX_E_INVALID;
    }

    if (task->fd_table[fd]) {
        vfs_file_t* old = (vfs_file_t*)task->fd_table[fd];
        vfs_close(old);
        kfree(old);
        task->fd_table[fd] = NULL;
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
        vfs_file_t* src = (vfs_file_t*)parent->fd_table[fd];
        if (!src) {
            child->fd_table[fd] = NULL;
            child->fd_flags[fd] = 0;
            continue;
        }

        vfs_file_t* copy = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
        if (!copy) {
            for (int i = 0; i < TASK_MAX_FD; i++) {
                vfs_file_t* old = (vfs_file_t*)child->fd_table[i];
                if (old) {
                    vfs_close(old);
                    kfree(old);
                    child->fd_table[i] = NULL;
                    child->fd_flags[i] = 0;
                }
            }
            return RDNX_E_NOMEM;
        }

        *copy = *src;
        child->fd_table[fd] = copy;
        child->fd_flags[fd] = parent->fd_flags[fd];
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
        vfs_file_t* file = (vfs_file_t*)task->fd_table[fd];
        if (file) {
            vfs_close(file);
            kfree(file);
        }
        task->fd_table[fd] = NULL;
        task->fd_flags[fd] = 0;
    }
}

uint64_t unix_fs_open(uint64_t user_path_ptr, uint64_t flags)
{
    const char* path = (const char*)(uintptr_t)user_path_ptr;
    char path_buf[UNIX_PATH_MAX];
    if (unix_copy_user_cstr(path_buf, sizeof(path_buf), path) != RDNX_OK) {
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
    return (uint64_t)fd;
}

uint64_t unix_fs_close(uint64_t fd)
{
    /* CT-007: close(fd) must invalidate descriptor for subsequent I/O. */
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)fd);
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_close(file);
    kfree(file);
    task_fd_close(task, (int)fd);
    return (uint64_t)RDNX_OK;
}

uint64_t unix_fs_read(uint64_t fd, uint64_t user_buf_ptr, uint64_t len)
{
    /* CT-008: read/write are valid only for open/valid descriptors. */
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)fd);
    void* buf = (void*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)len;
    if (!unix_user_range_ok(buf, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    int ret = vfs_read(file, buf, n);
    return (ret < 0) ? (uint64_t)RDNX_E_GENERIC : (uint64_t)ret;
}

uint64_t unix_fs_write(uint64_t fd, uint64_t user_buf_ptr, uint64_t len)
{
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, (int)fd);
    const void* buf = (const void*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)len;
    if (!unix_user_range_ok(buf, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_io_range_mapped(task, buf, n, false)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    int ret = vfs_write(file, buf, n);
    return (ret < 0) ? (uint64_t)RDNX_E_GENERIC : (uint64_t)ret;
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

uint64_t unix_fs_stat(uint64_t user_path_ptr, uint64_t user_stat_ptr)
{
    const char* path = (const char*)(uintptr_t)user_path_ptr;
    unix_stat_u_t* ustat = (unix_stat_u_t*)(uintptr_t)user_stat_ptr;
    char path_buf[UNIX_PATH_MAX];
    vfs_stat_t st;
    int rc;

    if (!unix_user_range_ok(ustat, sizeof(*ustat))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (unix_copy_user_cstr(path_buf, sizeof(path_buf), path) != RDNX_OK) {
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
