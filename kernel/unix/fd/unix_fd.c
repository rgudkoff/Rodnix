#include "../unix_layer.h"
#include "../../fs/vfs.h"
#include "../../common/heap.h"
#include "../../../include/error.h"

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
    if (!file) {
        return (uint64_t)RDNX_E_INVALID;
    }
    int ret = vfs_write(file, buf, n);
    return (ret < 0) ? (uint64_t)RDNX_E_GENERIC : (uint64_t)ret;
}
