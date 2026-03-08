#include "posix_syscall.h"
#include "../core/task.h"
#include "../common/security.h"
#include "../fabric/fabric.h"
#include "../fabric/device/device.h"
#include "../fabric/bus/pci.h"
#include "../fabric/service/net_service.h"
#include "../fs/vfs.h"
#include "../vm/vm_map.h"
#include "../unix/unix_layer.h"
#include "../../include/error.h"
#include "../../include/version.h"
#include "../../include/utsname.h"
#include "../../include/common.h"
#include "../arch/x86_64/config.h"
#include <stddef.h>

static posix_syscall_fn_t posix_table[POSIX_SYSCALL_MAX];

int posix_bind_stdio_to_console(task_t* task)
{
    return unix_bind_stdio_to_console(task);
}

static uint64_t posix_nosys(uint64_t a1,
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
    return (uint64_t)RDNX_E_UNSUPPORTED;
}

static uint64_t posix_getpid(uint64_t a1,
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
    task_t* task = task_get_current();
    return task ? task->task_id : 0;
}

static uint64_t posix_getuid(uint64_t a1,
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
    task_t* task = task_get_current();
    return task ? task->uid : 0;
}

static uint64_t posix_geteuid(uint64_t a1,
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
    task_t* task = task_get_current();
    return task ? task->euid : 0;
}

static uint64_t posix_getgid(uint64_t a1,
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
    task_t* task = task_get_current();
    return task ? task->gid : 0;
}

static uint64_t posix_getegid(uint64_t a1,
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
    task_t* task = task_get_current();
    return task ? task->egid : 0;
}

static uint64_t posix_setuid(uint64_t a1,
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
    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)RDNX_E_DENIED;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task_set_ids(task, (uint32_t)a1, task->gid, task->euid, task->egid);
    return (uint64_t)RDNX_OK;
}

static uint64_t posix_seteuid(uint64_t a1,
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
    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)RDNX_E_DENIED;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task_set_ids(task, task->uid, task->gid, (uint32_t)a1, task->egid);
    return (uint64_t)RDNX_OK;
}

static uint64_t posix_setgid(uint64_t a1,
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
    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)RDNX_E_DENIED;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task_set_ids(task, task->uid, (uint32_t)a1, task->euid, task->egid);
    return (uint64_t)RDNX_OK;
}

static uint64_t posix_setegid(uint64_t a1,
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
    if (security_check_euid(0) != SEC_OK) {
        return (uint64_t)RDNX_E_DENIED;
    }
    task_t* task = task_get_current();
    if (!task) {
        return (uint64_t)RDNX_E_INVALID;
    }
    task_set_ids(task, task->uid, task->gid, task->euid, (uint32_t)a1);
    return (uint64_t)RDNX_OK;
}

static uint64_t posix_open(uint64_t a1,
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
    return unix_fs_open(a1, a2);
}

static uint64_t posix_close(uint64_t a1,
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
    return unix_fs_close(a1);
}

static uint64_t posix_fcntl(uint64_t a1,
                            uint64_t a2,
                            uint64_t a3,
                            uint64_t a4,
                            uint64_t a5,
                            uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    return unix_fs_fcntl(a1, a2, a3);
}

static uint64_t posix_read(uint64_t a1,
                           uint64_t a2,
                           uint64_t a3,
                           uint64_t a4,
                           uint64_t a5,
                           uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    return unix_fs_read(a1, a2, a3);
}

static uint64_t posix_exit(uint64_t a1,
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

static uint64_t posix_exec(uint64_t a1,
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
    return unix_fs_exec(a1);
}

static uint64_t posix_spawn(uint64_t a1,
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
    return unix_proc_spawn(a1, a2);
}

static uint64_t posix_waitpid(uint64_t a1,
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

static uint64_t posix_write(uint64_t a1,
                            uint64_t a2,
                            uint64_t a3,
                            uint64_t a4,
                            uint64_t a5,
                            uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    return unix_fs_write(a1, a2, a3);
}

static uint64_t posix_uname(uint64_t a1,
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
    utsname_t* u = (utsname_t*)(uintptr_t)a1;
    if (!unix_user_range_ok(u, sizeof(*u))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    memset(u, 0, sizeof(*u));
    u->hdr = RDNX_ABI_INIT(utsname_t);
    strncpy(u->sysname, RODNIX_SYSNAME, sizeof(u->sysname) - 1);
    strncpy(u->nodename, RODNIX_NODENAME, sizeof(u->nodename) - 1);
    strncpy(u->release, RODNIX_RELEASE, sizeof(u->release) - 1);
    strncpy(u->version, RODNIX_VERSION, sizeof(u->version) - 1);
    strncpy(u->machine, X86_64_MACHINE, sizeof(u->machine) - 1);
    return (uint64_t)RDNX_OK;
}

static uint64_t posix_readdir(uint64_t a1,
                              uint64_t a2,
                              uint64_t a3,
                              uint64_t a4,
                              uint64_t a5,
                              uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    return unix_fs_readdir(a1, a2, a3);
}

static uint64_t posix_netiflist(uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    fabric_netif_info_t* user_entries = (fabric_netif_info_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;

    if (max_entries == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(fabric_netif_info_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t total = fabric_netif_count();
    uint32_t n = (total < max_entries) ? total : max_entries;
    for (uint32_t i = 0; i < n; i++) {
        fabric_netif_info_t info;
        if (fabric_netif_get_info(i, &info) != RDNX_OK) {
            break;
        }
        user_entries[i] = info;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

typedef struct hwdev_info {
    char name[32];
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t attached;
    uint8_t is_pci;
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint8_t pci_revision;
    uint8_t pci_header_type;
    uint16_t pci_command;
    uint16_t pci_status;
    uint32_t bars[PCI_BAR_COUNT];
} hwdev_info_t;

static uint64_t posix_hwlist(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    hwdev_info_t* user_entries = (hwdev_info_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;

    if (max_entries == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(hwdev_info_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t total = fabric_device_count();
    uint32_t n = (total < max_entries) ? total : max_entries;
    for (uint32_t i = 0; i < n; i++) {
        fabric_device_t* dev = fabric_device_get(i);
        if (!dev) {
            break;
        }

        hwdev_info_t info;
        memset(&info, 0, sizeof(info));
        if (dev->name) {
            strncpy(info.name, dev->name, sizeof(info.name) - 1);
        }
        info.vendor_id = dev->vendor_id;
        info.device_id = dev->device_id;
        info.class_code = dev->class_code;
        info.subclass = dev->subclass;
        info.prog_if = dev->prog_if;
        info.attached = dev->driver_state ? 1u : 0u;

        if (dev->bus_private && dev->name && strcmp(dev->name, "pci-device") == 0) {
            const pci_device_info_t* pci = (const pci_device_info_t*)dev->bus_private;
            info.is_pci = 1u;
            info.pci_bus = pci->bus;
            info.pci_device = pci->device;
            info.pci_function = pci->function;
            info.pci_revision = pci->revision_id;
            info.pci_header_type = pci->header_type;
            info.pci_command = pci->command;
            info.pci_status = pci->status;
            for (uint32_t bar = 0; bar < PCI_BAR_COUNT; bar++) {
                info.bars[bar] = pci->bars[bar];
            }
        }

        user_entries[i] = info;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

static uint64_t posix_fabricls(uint64_t a1,
                               uint64_t a2,
                               uint64_t a3,
                               uint64_t a4,
                               uint64_t a5,
                               uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    fabric_node_info_t* user_entries = (fabric_node_info_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;

    if (max_entries == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(fabric_node_info_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t total = 0;
    int n = fabric_node_list(user_entries, max_entries, &total);
    if (n < 0) {
        return (uint64_t)n;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

static uint64_t posix_fabricevents(uint64_t a1,
                                   uint64_t a2,
                                   uint64_t a3,
                                   uint64_t a4,
                                   uint64_t a5,
                                   uint64_t a6)
{
    (void)a5;
    (void)a6;
    fabric_event_t* user_entries = (fabric_event_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_read = (uint32_t*)(uintptr_t)a3;
    uint32_t* user_dropped = (uint32_t*)(uintptr_t)a4;

    if (max_entries == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(fabric_event_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_read && !unix_user_range_ok(user_read, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_dropped && !unix_user_range_ok(user_dropped, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t read = 0;
    uint32_t dropped = 0;
    int rc = fabric_event_drain(user_entries, max_entries, &read, &dropped);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    if (user_read) {
        *user_read = read;
    }
    if (user_dropped) {
        *user_dropped = dropped;
    }
    return (uint64_t)read;
}

static uint64_t posix_mmap(uint64_t a1,
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

static uint64_t posix_munmap(uint64_t a1,
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

static uint64_t posix_brk(uint64_t a1,
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

static uint64_t posix_fork(uint64_t a1,
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

void posix_syscall_init(void)
{
    for (uint32_t i = 0; i < POSIX_SYSCALL_MAX; i++) {
        posix_table[i] = NULL;
    }
#define POSIX_REGISTER(num, fn) posix_syscall_register((num), (fn))
#include "posix_sysent.inc"
#undef POSIX_REGISTER
}

int posix_syscall_register(uint32_t num, posix_syscall_fn_t fn)
{
    if (num >= POSIX_SYSCALL_MAX || !fn) {
        return RDNX_E_INVALID;
    }
    posix_table[num] = fn;
    return RDNX_OK;
}

uint64_t posix_syscall_dispatch(uint64_t num,
                                uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    if (num >= POSIX_SYSCALL_MAX || !posix_table[num]) {
        return (uint64_t)RDNX_E_UNSUPPORTED;
    }
    return posix_table[num](a1, a2, a3, a4, a5, a6);
}
