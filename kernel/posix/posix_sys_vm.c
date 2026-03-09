#include "posix_sys_vm.h"
#include "posix_syscall.h"
#include "../fs/vfs.h"
#include "../vm/vm_map.h"
#include "../unix/unix_layer.h"
#include "../../include/error.h"

uint64_t posix_mmap(uint64_t a1,
                           uint64_t a2,
                           uint64_t a3,
                           uint64_t a4,
                           uint64_t a5,
                           uint64_t a6)
{
    (void)a5;
    (void)a6;
    enum {
        PROT_READ = 0x1,
        PROT_WRITE = 0x2,
        PROT_EXEC = 0x4,
        MAP_SHARED = 0x0001,
        MAP_PRIVATE = 0x0002,
        MAP_FIXED = 0x0010,
        MAP_ANON = 0x1000
    };

    task_t* task = task_get_current();
    if (!task || a2 == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t prot = VM_PROT_NONE;
    if (a3 & PROT_READ) {
        prot |= VM_PROT_READ;
    }
    if (a3 & PROT_WRITE) {
        prot |= VM_PROT_WRITE;
    }
    if (a3 & PROT_EXEC) {
        prot |= VM_PROT_EXEC;
    }
    if (prot == VM_PROT_NONE) {
        prot = VM_PROT_READ;
    }

    uint32_t flags = 0;
    if (a4 & MAP_PRIVATE) {
        flags |= VM_MAP_F_PRIVATE;
    }
    if (a4 & MAP_FIXED) {
        flags |= VM_MAP_F_FIXED;
    }
    if (a4 & MAP_ANON) {
        flags |= VM_MAP_F_ANON;
    }
    if ((flags & VM_MAP_F_ANON) != 0) {
        long ret = vm_task_mmap(task, a1, a2, prot, flags);
        return (uint64_t)ret;
    }

    if ((a4 & (MAP_PRIVATE | MAP_SHARED)) == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    int fd = (int)a5;
    if (fd < 0 || fd >= TASK_MAX_FD || task->fd_kind[fd] != UNIX_FD_KIND_VFS) {
        return (uint64_t)RDNX_E_INVALID;
    }
    uint64_t off = a6;
    vfs_file_t* file = (vfs_file_t*)task_fd_get(task, fd);
    if (!file || !file->node || !file->node->inode || file->node->type != VFS_NODE_FILE) {
        return (uint64_t)RDNX_E_INVALID;
    }
    const uint8_t* data = file->node->inode->data;
    uint64_t data_size = (uint64_t)file->node->inode->size;
    if (!data) {
        return (uint64_t)RDNX_E_INVALID;
    }
    long ret = vm_task_mmap_file(task, a1, a2, prot, flags, data, data_size, off);
    return (uint64_t)ret;
}

uint64_t posix_munmap(uint64_t a1,
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
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vm_task_munmap(task, a1, a2);
}

uint64_t posix_brk(uint64_t a1,
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
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)vm_task_brk(task, a1);
}
