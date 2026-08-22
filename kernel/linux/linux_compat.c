#include "linux_compat.h"
#include "linux_errno.h"
#include "../posix/posix_sys_file.h"
#include "../posix/posix_sys_ids.h"
#include "../posix/posix_sys_info.h"
#include "../posix/posix_sys_proc.h"
#include "../posix/posix_sys_vm.h"
#include "../core/task.h"
#include "../../fs/vfs.h"
#include "../../include/sys/file.h"
#include "../unix/unix_layer.h"
#include "../arch/pmm.h"
#include "../../mm/vm_map.h"
#include "../../console/tty_console.h"
#include "../../include/error.h"
#include "../../include/common.h"
#include "../../include/console.h"

/* Shadow of the current thread's FS.Base — kept in sync so ISR/IRQ stubs can
 * re-apply it after loading kernel segment selectors (which zero the base). */
extern uint64_t g_current_tls_fs_base;

enum {
    IA32_FS_BASE_MSR = 0xC0000100,
    LINUX_ARCH_SET_FS = 0x1002,
    LINUX_ARCH_GET_FS = 0x1003,
    LINUX_SIGSET_SIZE = 8,
    LINUX_O_ACCMODE = 00000003,
    LINUX_O_RDONLY = 00000000,
    LINUX_O_WRONLY = 00000001,
    LINUX_O_RDWR = 00000002,
    LINUX_O_CREAT = 00000100,
    LINUX_O_TRUNC = 00001000,
    LINUX_O_APPEND = 00002000,
    LINUX_AT_FDCWD = -100,
    LINUX_PAGE_SIZE = 4096,
    LINUX_DT_DIR = 4,
    LINUX_DT_REG = 8,
    LINUX_F_OK = 0,
    LINUX_X_OK = 1,
    LINUX_W_OK = 2,
    LINUX_R_OK = 4,
    LINUX_ACCESS_MODE_MASK = (LINUX_R_OK | LINUX_W_OK | LINUX_X_OK),
    LINUX_AT_SYMLINK_NOFOLLOW = 0x100,
    LINUX_AT_EACCESS = 0x200,
    LINUX_AT_EMPTY_PATH = 0x1000,
};

typedef struct linux_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
} linux_timeval_t;

typedef struct linux_sysinfo_u {
    int64_t uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
    uint8_t _f[4];
} linux_sysinfo_u_t;

typedef struct linux_iovec_u {
    uint64_t iov_base;
    uint64_t iov_len;
} linux_iovec_u_t;

typedef struct linux_dirent64_u {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
} linux_dirent64_u_t;

typedef struct linux_dirent_u {
    uint64_t d_ino;
    uint64_t d_off;
    uint16_t d_reclen;
    char d_name[];
} linux_dirent_u_t;

typedef struct linux_symlink_entry {
    uint8_t used;
    char link_path[UNIX_PATH_MAX];
    char target[UNIX_PATH_MAX];
} linux_symlink_entry_t;

typedef struct linux_mode_entry {
    uint8_t used;
    char path[UNIX_PATH_MAX];
    uint16_t mode;
} linux_mode_entry_t;

enum {
    LINUX_SYMLINK_MAX = 64,
    LINUX_MODE_MAX = 128
};

static linux_symlink_entry_t g_linux_symlinks[LINUX_SYMLINK_MAX];
static linux_mode_entry_t g_linux_modes[LINUX_MODE_MAX];

typedef struct linux_trace_entry {
    uint64_t seq;
    uint64_t num;
    uint64_t a1;
    uint64_t a2;
    uint64_t a3;
    uint64_t a4;
    uint64_t a5;
    uint64_t a6;
    uint64_t task_id;
} linux_trace_entry_t;

enum {
    LINUX_TRACE_RING = 128
};

static linux_trace_entry_t g_linux_trace[LINUX_TRACE_RING];
static uint32_t g_linux_trace_head = 0;
static uint64_t g_linux_trace_seq = 0;

static void linux_trace_record(uint64_t num,
                               uint64_t a1,
                               uint64_t a2,
                               uint64_t a3,
                               uint64_t a4,
                               uint64_t a5,
                               uint64_t a6)
{
    uint32_t i = g_linux_trace_head;
    g_linux_trace[i].seq = ++g_linux_trace_seq;
    g_linux_trace[i].num = num;
    g_linux_trace[i].a1 = a1;
    g_linux_trace[i].a2 = a2;
    g_linux_trace[i].a3 = a3;
    g_linux_trace[i].a4 = a4;
    g_linux_trace[i].a5 = a5;
    g_linux_trace[i].a6 = a6;
    task_t* _tr_task = task_get_current();
    g_linux_trace[i].task_id = _tr_task ? _tr_task->task_id : 0;
    g_linux_trace_head = (i + 1u) % LINUX_TRACE_RING;
}

void linux_compat_trace_dump_recent(void)
{
    kputs("[LNXTRACE] recent syscalls:\n");
    uint32_t start = g_linux_trace_head;
    for (uint32_t n = 0; n < LINUX_TRACE_RING; n++) {
        uint32_t i = (start + n) % LINUX_TRACE_RING;
        if (g_linux_trace[i].seq == 0) {
            continue;
        }
        kprintf("[LNXTRACE] #%llu tid=%llu n=%llu a1=%llx a2=%llx a3=%llx a4=%llx a5=%llx a6=%llx\n",
                (unsigned long long)g_linux_trace[i].seq,
                (unsigned long long)g_linux_trace[i].task_id,
                (unsigned long long)g_linux_trace[i].num,
                (unsigned long long)g_linux_trace[i].a1,
                (unsigned long long)g_linux_trace[i].a2,
                (unsigned long long)g_linux_trace[i].a3,
                (unsigned long long)g_linux_trace[i].a4,
                (unsigned long long)g_linux_trace[i].a5,
                (unsigned long long)g_linux_trace[i].a6);
    }
}

static int linux_symlink_find(const char* link_path)
{
    if (!link_path) {
        return -1;
    }
    for (int i = 0; i < LINUX_SYMLINK_MAX; i++) {
        if (!g_linux_symlinks[i].used) {
            continue;
        }
        if (strcmp(g_linux_symlinks[i].link_path, link_path) == 0) {
            return i;
        }
    }
    return -1;
}

static int linux_symlink_alloc_slot(void)
{
    for (int i = 0; i < LINUX_SYMLINK_MAX; i++) {
        if (!g_linux_symlinks[i].used) {
            return i;
        }
    }
    return -1;
}

static int linux_symlink_add(const char* link_path, const char* target)
{
    int idx;
    if (!link_path || !target || link_path[0] == '\0' || target[0] == '\0') {
        return RDNX_E_INVALID;
    }
    if (linux_symlink_find(link_path) >= 0) {
        return RDNX_E_BUSY;
    }
    idx = linux_symlink_alloc_slot();
    if (idx < 0) {
        return RDNX_E_BUSY;
    }
    g_linux_symlinks[idx].used = 1;
    strncpy(g_linux_symlinks[idx].link_path, link_path, sizeof(g_linux_symlinks[idx].link_path) - 1);
    g_linux_symlinks[idx].link_path[sizeof(g_linux_symlinks[idx].link_path) - 1] = '\0';
    strncpy(g_linux_symlinks[idx].target, target, sizeof(g_linux_symlinks[idx].target) - 1);
    g_linux_symlinks[idx].target[sizeof(g_linux_symlinks[idx].target) - 1] = '\0';
    return RDNX_OK;
}

static int linux_symlink_remove(const char* link_path)
{
    int idx = linux_symlink_find(link_path);
    if (idx < 0) {
        return RDNX_E_NOTFOUND;
    }
    memset(&g_linux_symlinks[idx], 0, sizeof(g_linux_symlinks[idx]));
    return RDNX_OK;
}

static void linux_symlink_rename_path(const char* old_path, const char* new_path)
{
    int idx = linux_symlink_find(old_path);
    if (idx < 0 || !new_path || new_path[0] == '\0') {
        return;
    }
    strncpy(g_linux_symlinks[idx].link_path, new_path, sizeof(g_linux_symlinks[idx].link_path) - 1);
    g_linux_symlinks[idx].link_path[sizeof(g_linux_symlinks[idx].link_path) - 1] = '\0';
}

static int linux_mode_find(const char* path)
{
    if (!path) {
        return -1;
    }
    for (int i = 0; i < LINUX_MODE_MAX; i++) {
        if (!g_linux_modes[i].used) {
            continue;
        }
        if (strcmp(g_linux_modes[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int linux_mode_alloc_slot(void)
{
    for (int i = 0; i < LINUX_MODE_MAX; i++) {
        if (!g_linux_modes[i].used) {
            return i;
        }
    }
    return -1;
}

static void linux_mode_set(const char* path, uint16_t mode)
{
    int idx;
    if (!path || path[0] == '\0') {
        return;
    }
    idx = linux_mode_find(path);
    if (idx < 0) {
        idx = linux_mode_alloc_slot();
        if (idx < 0) {
            return;
        }
        g_linux_modes[idx].used = 1;
        strncpy(g_linux_modes[idx].path, path, sizeof(g_linux_modes[idx].path) - 1);
        g_linux_modes[idx].path[sizeof(g_linux_modes[idx].path) - 1] = '\0';
    }
    g_linux_modes[idx].mode = (uint16_t)(mode & 0777u);
}

static void linux_mode_remove(const char* path)
{
    int idx = linux_mode_find(path);
    if (idx < 0) {
        return;
    }
    memset(&g_linux_modes[idx], 0, sizeof(g_linux_modes[idx]));
}

static void linux_mode_rename_path(const char* old_path, const char* new_path)
{
    int idx = linux_mode_find(old_path);
    if (idx < 0 || !new_path || new_path[0] == '\0') {
        return;
    }
    strncpy(g_linux_modes[idx].path, new_path, sizeof(g_linux_modes[idx].path) - 1);
    g_linux_modes[idx].path[sizeof(g_linux_modes[idx].path) - 1] = '\0';
}

static uint16_t linux_mode_get_or_default(const char* path)
{
    int idx = linux_mode_find(path);
    if (idx >= 0) {
        return g_linux_modes[idx].mode;
    }
    return 0777u;
}

static int linux_vfs_node_to_abspath(const vfs_node_t* node, char* out, size_t out_sz)
{
    const vfs_node_t* stack[64];
    size_t depth = 0;
    if (!node || !out || out_sz < 2) {
        return RDNX_E_INVALID;
    }
    const vfs_node_t* it = node;
    while (it && it->parent && depth < 64) {
        stack[depth++] = it;
        it = it->parent;
    }
    size_t p = 0;
    out[p++] = '/';
    for (size_t i = depth; i > 0; i--) {
        const char* name = stack[i - 1]->name;
        for (size_t k = 0; name && name[k] != '\0'; k++) {
            if (p + 1 >= out_sz) {
                return RDNX_E_INVALID;
            }
            out[p++] = name[k];
        }
        if (i > 1) {
            if (p + 1 >= out_sz) {
                return RDNX_E_INVALID;
            }
            out[p++] = '/';
        }
    }
    out[p] = '\0';
    return RDNX_OK;
}

static int linux_proc_fd_target(const char* path, char* out, size_t out_sz)
{
    task_t* t = task_get_current();
    const char* p = path;
    uint64_t pid = 0;
    uint64_t fd = 0;
    vfs_file_t* file;

    if (!t || !path || !out || out_sz < 2) {
        return RDNX_E_INVALID;
    }

    if (strncmp(p, "/proc/", 6) != 0) {
        return RDNX_E_NOTFOUND;
    }
    p += 6;
    if (strncmp(p, "self/fd/", 8) == 0) {
        p += 8;
    } else {
        if (*p < '0' || *p > '9') {
            return RDNX_E_NOTFOUND;
        }
        while (*p >= '0' && *p <= '9') {
            pid = (pid * 10u) + (uint64_t)(*p - '0');
            p++;
        }
        if (pid != t->task_id || strncmp(p, "/fd/", 4) != 0) {
            return RDNX_E_NOTFOUND;
        }
        p += 4;
    }

    if (*p < '0' || *p > '9') {
        return RDNX_E_NOTFOUND;
    }
    while (*p >= '0' && *p <= '9') {
        fd = (fd * 10u) + (uint64_t)(*p - '0');
        p++;
    }
    if (*p != '\0') {
        return RDNX_E_NOTFOUND;
    }
    /* было: task_proc(t)->fd_kind[fd] != UNIX_FD_KIND_VFS */
    rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(t), (int)fd);
    if (fd >= PROC_MAX_FD || !rf || rf->kind != UNIX_FD_KIND_VFS) {
        return RDNX_E_NOTFOUND;
    }

    file = (vfs_file_t*)rf->priv;
    if (!file || !file->node) {
        return RDNX_E_NOTFOUND;
    }
    return linux_vfs_node_to_abspath(file->node, out, out_sz);
}

static inline void linux_wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static inline uint64_t linux_ret(uint64_t native_ret)
{
    long r = (long)native_ret;
    if (r >= 0) {
        return (uint64_t)r;
    }
    return (uint64_t)(-(long)linux_errno_from_rdnx((int)r));
}

static int linux_to_rdnx_open_flags(int linux_flags)
{
    int out = 0;
    int acc = linux_flags & LINUX_O_ACCMODE;

    if (acc == LINUX_O_WRONLY || acc == LINUX_O_RDWR) {
        out |= VFS_OPEN_WRITE;
    }
    if (acc == LINUX_O_RDONLY || acc == LINUX_O_RDWR) {
        out |= VFS_OPEN_READ;
    }
    if (linux_flags & LINUX_O_CREAT) {
        out |= VFS_OPEN_CREATE;
    }
    if (linux_flags & LINUX_O_TRUNC) {
        out |= VFS_OPEN_TRUNC;
    }
    return out;
}

static uint64_t linux_to_rdnx_mmap_flags(uint64_t linux_flags)
{
    enum {
        LINUX_MAP_SHARED    = 0x01u,
        LINUX_MAP_PRIVATE   = 0x02u,
        LINUX_MAP_FIXED     = 0x10u,
        LINUX_MAP_ANONYMOUS = 0x20u,

        RDNX_MAP_SHARED     = 0x0001u,
        RDNX_MAP_PRIVATE    = 0x0002u,
        RDNX_MAP_FIXED      = 0x0010u,
        RDNX_MAP_ANON       = 0x1000u
    };

    uint64_t out = 0;
    if (linux_flags & LINUX_MAP_SHARED) {
        out |= RDNX_MAP_SHARED;
    }
    if (linux_flags & LINUX_MAP_PRIVATE) {
        out |= RDNX_MAP_PRIVATE;
    }
    if (linux_flags & LINUX_MAP_FIXED) {
        out |= RDNX_MAP_FIXED;
    }
    if (linux_flags & LINUX_MAP_ANONYMOUS) {
        out |= RDNX_MAP_ANON;
    }
    return out;
}

typedef struct linux_winsize_u {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} linux_winsize_u_t;

/* Linux x86_64 struct stat — 144 bytes (kernel ABI).
 * Note: st_nlink comes BEFORE st_mode. */
typedef struct linux_stat_u {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atim_sec;
    int64_t  st_atim_nsec;
    int64_t  st_mtim_sec;
    int64_t  st_mtim_nsec;
    int64_t  st_ctim_sec;
    int64_t  st_ctim_nsec;
    int64_t  _reserved[3];
} linux_stat_u_t;

/* Linux struct termios — 36 bytes (TCGETS/TCSETS ABI).
 * c_line (line discipline byte) sits between c_lflag and c_cc. */
typedef struct linux_termios_u {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
} linux_termios_u_t;

enum {
    LINUX_VINTR  = 0,
    LINUX_VERASE = 2,
    LINUX_VKILL  = 3,
    LINUX_VEOF   = 4,
    LINUX_VTIME  = 5,
    LINUX_VMIN   = 6,
};

enum {
    LINUX_IFLAG_INLCR = 0x00000040u,
    LINUX_IFLAG_IGNCR = 0x00000080u,
    LINUX_IFLAG_ICRNL = 0x00000100u,
};

enum {
    LINUX_OFLAG_OPOST = 0x00000001u,
    LINUX_OFLAG_ONLCR = 0x00000004u,
};

enum {
    LINUX_LFLAG_ISIG    = 0x00000001u,
    LINUX_LFLAG_ICANON  = 0x00000002u,
    LINUX_LFLAG_ECHO    = 0x00000008u,
    LINUX_LFLAG_ECHOCTL = 0x00000200u,
    LINUX_LFLAG_IEXTEN  = 0x00008000u,
};

static uint32_t linux_iflag_from_tty(uint32_t tty_iflag)
{
    uint32_t out = 0;
    if (tty_iflag & TTY_IFLAG_INLCR) {
        out |= LINUX_IFLAG_INLCR;
    }
    if (tty_iflag & TTY_IFLAG_IGNCR) {
        out |= LINUX_IFLAG_IGNCR;
    }
    if (tty_iflag & TTY_IFLAG_ICRNL) {
        out |= LINUX_IFLAG_ICRNL;
    }
    return out;
}

static uint32_t linux_iflag_to_tty(uint32_t linux_iflag)
{
    uint32_t out = 0;
    if (linux_iflag & LINUX_IFLAG_INLCR) {
        out |= TTY_IFLAG_INLCR;
    }
    if (linux_iflag & LINUX_IFLAG_IGNCR) {
        out |= TTY_IFLAG_IGNCR;
    }
    if (linux_iflag & LINUX_IFLAG_ICRNL) {
        out |= TTY_IFLAG_ICRNL;
    }
    return out;
}

static uint32_t linux_oflag_from_tty(uint32_t tty_oflag)
{
    uint32_t out = 0;
    if (tty_oflag & TTY_OFLAG_OPOST) {
        out |= LINUX_OFLAG_OPOST;
    }
    if (tty_oflag & TTY_OFLAG_ONLCR) {
        out |= LINUX_OFLAG_ONLCR;
    }
    return out;
}

static uint32_t linux_oflag_to_tty(uint32_t linux_oflag)
{
    uint32_t out = 0;
    if (linux_oflag & LINUX_OFLAG_OPOST) {
        out |= TTY_OFLAG_OPOST;
    }
    if (linux_oflag & LINUX_OFLAG_ONLCR) {
        out |= TTY_OFLAG_ONLCR;
    }
    return out;
}

static uint32_t linux_lflag_from_tty(uint32_t tty_lflag)
{
    uint32_t out = 0;
    if (tty_lflag & TTY_MODE_ISIG) {
        out |= LINUX_LFLAG_ISIG;
    }
    if (tty_lflag & TTY_MODE_ICANON) {
        out |= LINUX_LFLAG_ICANON;
    }
    if (tty_lflag & TTY_MODE_ECHO) {
        out |= LINUX_LFLAG_ECHO;
    }
    if (tty_lflag & TTY_MODE_ECHOCTL) {
        out |= LINUX_LFLAG_ECHOCTL;
    }
    if (tty_lflag & TTY_MODE_IEXTEN) {
        out |= LINUX_LFLAG_IEXTEN;
    }
    return out;
}

static uint32_t linux_lflag_to_tty(uint32_t linux_lflag)
{
    uint32_t out = 0;
    if (linux_lflag & LINUX_LFLAG_ISIG) {
        out |= TTY_MODE_ISIG;
    }
    if (linux_lflag & LINUX_LFLAG_ICANON) {
        out |= TTY_MODE_ICANON;
    }
    if (linux_lflag & LINUX_LFLAG_ECHO) {
        out |= TTY_MODE_ECHO;
    }
    if (linux_lflag & LINUX_LFLAG_ECHOCTL) {
        out |= TTY_MODE_ECHOCTL;
    }
    if (linux_lflag & LINUX_LFLAG_IEXTEN) {
        out |= TTY_MODE_IEXTEN;
    }
    return out;
}

static uint8_t linux_cc_from_tty(uint32_t linux_idx)
{
    switch (linux_idx) {
    case LINUX_VINTR:
        return tty_console_get_cc(TTY_VINTR);
    case LINUX_VERASE:
        return tty_console_get_cc(TTY_VERASE);
    case LINUX_VKILL:
        return tty_console_get_cc(TTY_VKILL);
    case LINUX_VEOF:
        return tty_console_get_cc(TTY_VEOF);
    case LINUX_VTIME:
        return tty_console_get_cc(TTY_VTIME);
    case LINUX_VMIN:
        return tty_console_get_cc(TTY_VMIN);
    default:
        return 0;
    }
}

static void linux_cc_to_tty(uint32_t linux_idx, uint8_t value)
{
    switch (linux_idx) {
    case LINUX_VINTR:
        tty_console_set_cc(TTY_VINTR, value);
        break;
    case LINUX_VERASE:
        tty_console_set_cc(TTY_VERASE, value);
        break;
    case LINUX_VKILL:
        tty_console_set_cc(TTY_VKILL, value);
        break;
    case LINUX_VEOF:
        tty_console_set_cc(TTY_VEOF, value);
        break;
    case LINUX_VTIME:
        tty_console_set_cc(TTY_VTIME, value);
        break;
    case LINUX_VMIN:
        tty_console_set_cc(TTY_VMIN, value);
        break;
    default:
        break;
    }
}

int linux_compat_init(void)
{
    return RDNX_OK;
}

void linux_compat_apply_user_state(void)
{
    task_t* task = task_get_current();
    if (task_get_abi(task) != TASK_ABI_LINUX) {
        return;
    }
    linux_wrmsr(IA32_FS_BASE_MSR, task->tls_fs_base);
    /* Keep the ISR/IRQ shadow in sync so any interrupt firing in user space
     * restores the correct FS.Base (not the stale thread->tls_fs_base = 0
     * that sched_arch_apply_thread set at context-switch time). */
    g_current_tls_fs_base = task->tls_fs_base;
}

uint64_t linux_compat_dispatch(uint64_t num,
                               uint64_t a1,
                               uint64_t a2,
                               uint64_t a3,
                               uint64_t a4,
                               uint64_t a5,
                               uint64_t a6)
{
    linux_trace_record(num, a1, a2, a3, a4, a5, a6);
    switch (num) {
    case 0:  /* read */
        return linux_ret(posix_read(a1, a2, a3, 0, 0, 0));
    case 1:  /* write */
        return linux_ret(posix_write(a1, a2, a3, 0, 0, 0));
    case 2:  /* open */
    {
        char _opath[UNIX_PATH_MAX];
        const char* _osrc = (const char*)(uintptr_t)a1;
        uint64_t _oflags = (uint64_t)linux_to_rdnx_open_flags((int)a2);
        /* /dev/tty → our console device */
        if (_osrc && unix_user_range_ok(_osrc, 9) &&
            _osrc[0]=='/' && _osrc[1]=='d' && _osrc[2]=='e' && _osrc[3]=='v' &&
            _osrc[4]=='/' && _osrc[5]=='t' && _osrc[6]=='t' && _osrc[7]=='y' && _osrc[8]=='\0') {
            static const char _tty_dev[] = "/dev/console";
            return linux_ret(unix_fs_open_kernel_path(_tty_dev, _oflags));
        }
        (void)_opath;
        return linux_ret(posix_open(a1, _oflags, 0, 0, 0, 0));
    }
    case 3:  /* close */
        return linux_ret(posix_close(a1, 0, 0, 0, 0, 0));
    case 4:  /* stat */
    case 6:  /* lstat (no symlinks yet — identical to stat) */
    {
        char path_buf[UNIX_PATH_MAX];
        vfs_stat_t st;
        linux_stat_u_t* ust = (linux_stat_u_t*)(uintptr_t)a2;
        if (!ust || !unix_user_range_ok(ust, sizeof(*ust))) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (unix_resolve_user_path((const char*)(uintptr_t)a1, path_buf, sizeof(path_buf)) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        int _rc = vfs_stat(path_buf, &st);
        if (_rc != RDNX_OK) {
            return linux_ret((uint64_t)_rc);
        }
        linux_stat_u_t kst;
        memset(&kst, 0, sizeof(kst));
        kst.st_dev     = 1;
        kst.st_ino     = 1;
        kst.st_nlink   = 1;
        kst.st_mode    = st.mode;
        kst.st_uid     = st.uid;
        kst.st_gid     = st.gid;
        kst.st_size    = (int64_t)st.size;
        kst.st_blksize = 4096;
        kst.st_blocks  = (int64_t)((st.size + 511) / 512);
        kst.st_mtim_sec = (int64_t)st.mtime;
        kst.st_atim_sec = (int64_t)st.mtime;
        kst.st_ctim_sec = (int64_t)st.mtime;
        if (unix_copy_to_user(ust, &kst, sizeof(kst)) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        return 0;
    }
    case 5:  /* fstat */
    {
        task_t* _t = task_get_current();
        linux_stat_u_t* ust = (linux_stat_u_t*)(uintptr_t)a2;
        if (!_t || !ust || !unix_user_range_ok(ust, sizeof(*ust))) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        int _fd = (int)a1;
        if (_fd < 0 || _fd >= PROC_MAX_FD) {
            return (uint64_t)(-LINUX_EBADF);
        }
        /* было: task_proc(_t)->fd_kind[_fd] != UNIX_FD_KIND_VFS */
        rdnx_file_t* _rf = (rdnx_file_t*)proc_fd_get(task_proc(_t), _fd);
        if (!_rf || _rf->kind != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EBADF);
        }
        vfs_file_t* _f = (vfs_file_t*)_rf->priv;
        if (!_f || !_f->node) {
            return (uint64_t)(-LINUX_EBADF);
        }
        vfs_stat_t st;
        if (vfs_fstat(_f, &st) != RDNX_OK) {
            memset(&st, 0, sizeof(st));
            st.mode = (_f->node->type == VFS_NODE_DIR) ? 0040755u : 0100644u;
            if (_f->node->inode && (_f->node->inode->flags & VFS_INODE_CONSOLE)) {
                st.mode = 0020620u; /* character device */
            }
            if (_f->node->inode) { st.size = _f->node->inode->size; }
        }
        linux_stat_u_t kst;
        memset(&kst, 0, sizeof(kst));
        kst.st_dev     = 1;
        kst.st_ino     = 1;
        kst.st_nlink   = 1;
        kst.st_mode    = st.mode;
        kst.st_uid     = st.uid;
        kst.st_gid     = st.gid;
        kst.st_size    = (int64_t)st.size;
        kst.st_blksize = 4096;
        kst.st_blocks  = (int64_t)((st.size + 511) / 512);
        kst.st_mtim_sec = (int64_t)st.mtime;
        kst.st_atim_sec = (int64_t)st.mtime;
        kst.st_ctim_sec = (int64_t)st.mtime;
        if (unix_copy_to_user(ust, &kst, sizeof(kst)) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        return 0;
    }
    case 7:  /* poll */
        return linux_ret(posix_poll(a1, a2, a3, 0, 0, 0));
    case 13: /* rt_sigaction */
        return linux_ret(posix_sigaction(a1, a2, a3, 0, 0, 0));
    case 14: { /* rt_sigprocmask (minimal compatibility shim) */
        void* set = (void*)(uintptr_t)a2;
        void* oldset = (void*)(uintptr_t)a3;
        uint64_t sigsetsize = a4;

        if (sigsetsize != LINUX_SIGSET_SIZE) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (set && !unix_user_range_ok(set, (size_t)sigsetsize)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (oldset) {
            if (!unix_user_range_ok(oldset, (size_t)sigsetsize)) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            memset(oldset, 0, (size_t)sigsetsize);
        }
        return 0;
    }
    case 15: /* rt_sigreturn */
        return linux_ret(posix_sigreturn(0, 0, 0, 0, 0, 0));
    case 23: /* select */
        return linux_ret(posix_select(a1, a2, a3, a4, a5, 0));
    case 26: /* msync */
        return linux_ret(posix_msync(a1, a2, a3, 0, 0, 0));
    case 35: /* nanosleep */
        return linux_ret(posix_nanosleep(a1, a2, 0, 0, 0, 0));
    case 8:  /* lseek */
        return linux_ret(posix_lseek(a1, a2, a3, 0, 0, 0));
    case 9:  /* mmap */
        return linux_ret(posix_mmap(a1, a2, a3, linux_to_rdnx_mmap_flags(a4), a5, a6));
    case 10: { /* mprotect */
        uint32_t prot = VM_PROT_NONE;
        if (a3 & 0x1u) prot |= VM_PROT_READ;
        if (a3 & 0x2u) prot |= VM_PROT_WRITE;
        if (a3 & 0x4u) prot |= VM_PROT_EXEC;
        return linux_ret((uint64_t)vm_task_mprotect(task_get_current(), a1, a2, prot));
    }
    case 11: /* munmap */
        return linux_ret(posix_munmap(a1, a2, 0, 0, 0, 0));
    case 12: /* brk */
        return linux_ret(posix_brk(a1, 0, 0, 0, 0, 0));
    case 16: { /* ioctl */
        enum {
            LINUX_TIOCGWINSZ = 0x5413u,
            LINUX_TIOCSWINSZ = 0x5414u,
            LINUX_TCGETS     = 0x5401u,
            LINUX_TCSETS     = 0x5402u,
            LINUX_TCSETSW    = 0x5403u,
            LINUX_TCSETSF    = 0x5404u,
            LINUX_TIOCGPGRP  = 0x540Fu,
            LINUX_TIOCSPGRP  = 0x5410u,
            LINUX_TIOCGPTN   = 0x80045430u,
        };
        uint32_t req = (uint32_t)a2;
        if (req == LINUX_TIOCGWINSZ) {
            linux_winsize_u_t* ws = (linux_winsize_u_t*)(uintptr_t)a3;
            if (!ws || !unix_user_range_ok(ws, sizeof(*ws))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            linux_winsize_u_t kws;
            tty_console_get_winsize(&kws.ws_row, &kws.ws_col,
                                    &kws.ws_xpixel, &kws.ws_ypixel);
            if (unix_copy_to_user(ws, &kws, sizeof(kws)) != RDNX_OK) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            return 0;
        }
        if (req == LINUX_TIOCSWINSZ) {
            linux_winsize_u_t* ws = (linux_winsize_u_t*)(uintptr_t)a3;
            linux_winsize_u_t kws;
            if (!ws || !unix_user_range_ok(ws, sizeof(*ws))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            if (unix_copy_from_user(&kws, ws, sizeof(kws)) != RDNX_OK) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            tty_console_set_winsize(kws.ws_row, kws.ws_col,
                                    kws.ws_xpixel, kws.ws_ypixel);
            return 0;
        }
        if (req == LINUX_TCGETS) {
            linux_termios_u_t* lt = (linux_termios_u_t*)(uintptr_t)a3;
            if (!lt || !unix_user_range_ok(lt, sizeof(*lt))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            uint64_t rc = posix_ioctl(a1, 0x7401u /* UNIX_TTY_IOCTL_ISATTY */, 0, 0, 0, 0);
            if ((int64_t)rc < 0) {
                return (uint64_t)(-LINUX_ENOTTY);
            }
            linux_termios_u_t klt;
            memset(&klt, 0, sizeof(klt));
            klt.c_iflag = linux_iflag_from_tty(tty_console_get_iflag());
            klt.c_oflag = linux_oflag_from_tty(tty_console_get_oflag());
            klt.c_lflag = linux_lflag_from_tty(tty_console_get_lflag());
            klt.c_line  = 0; /* N_TTY */
            for (int _i = 0; _i < 19; _i++) {
                klt.c_cc[_i] = linux_cc_from_tty((uint32_t)_i);
            }
            if (unix_copy_to_user(lt, &klt, sizeof(klt)) != RDNX_OK) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            return 0;
        }
        if (req == LINUX_TCSETS || req == LINUX_TCSETSW || req == LINUX_TCSETSF) {
            linux_termios_u_t* lt = (linux_termios_u_t*)(uintptr_t)a3;
            if (!lt || !unix_user_range_ok(lt, sizeof(*lt))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            linux_termios_u_t klt;
            if (unix_copy_from_user(&klt, lt, sizeof(klt)) != RDNX_OK) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            uint64_t rc = posix_ioctl(a1, 0x7401u /* UNIX_TTY_IOCTL_ISATTY */, 0, 0, 0, 0);
            if ((int64_t)rc < 0) {
                return (uint64_t)(-LINUX_ENOTTY);
            }
            tty_console_set_iflag(linux_iflag_to_tty(klt.c_iflag));
            tty_console_set_oflag(linux_oflag_to_tty(klt.c_oflag));
            tty_console_set_lflag(linux_lflag_to_tty(klt.c_lflag));
            for (int _i = 0; _i < 19; _i++) {
                linux_cc_to_tty((uint32_t)_i, klt.c_cc[_i]);
            }
            return 0;
        }
        if (req == LINUX_TIOCGPGRP) {
            int32_t* pgid_ptr = (int32_t*)(uintptr_t)a3;
            if (!pgid_ptr || !unix_user_range_ok(pgid_ptr, sizeof(*pgid_ptr))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            task_t* _ct = task_get_current();
            uint64_t fg = tty_console_get_fg_pgrp();
            if (fg == 0 && _ct) {
                fg = task_proc(_ct)->process_group_id;
            }
            *pgid_ptr = (int32_t)fg;
            return 0;
        }
        if (req == LINUX_TIOCSPGRP) {
            int32_t* pgid_ptr = (int32_t*)(uintptr_t)a3;
            int32_t pgid = 0;
            if (!pgid_ptr || !unix_user_range_ok(pgid_ptr, sizeof(*pgid_ptr))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            if (unix_copy_from_user(&pgid, pgid_ptr, sizeof(pgid)) != RDNX_OK || pgid <= 0) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            tty_console_set_fg_pgrp((uint64_t)(uint32_t)pgid);
            return 0;
        }
        return linux_ret(posix_ioctl(a1, a2, a3, 0, 0, 0));
    }
    case 21: { /* access */
        const char* path = (const char*)(uintptr_t)a1;
        int mode = (int)a2;
        vfs_stat_t st;
        uint16_t m;
        if (!path) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if ((mode & ~LINUX_ACCESS_MODE_MASK) != 0) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (linux_symlink_find(path) >= 0 || vfs_stat(path, &st) == RDNX_OK) {
            if (mode == LINUX_F_OK) {
                return 0;
            }
            m = linux_mode_get_or_default(path);
            if ((mode & LINUX_R_OK) && (m & 0444u) == 0) {
                return (uint64_t)(-LINUX_EACCES);
            }
            if ((mode & LINUX_W_OK) && (m & 0222u) == 0) {
                return (uint64_t)(-LINUX_EACCES);
            }
            if ((mode & LINUX_X_OK) && (m & 0111u) == 0) {
                return (uint64_t)(-LINUX_EACCES);
            }
            return 0;
        }
        return (uint64_t)(-LINUX_ENOENT);
    }
    case 32: /* dup */
        return linux_ret(posix_dup(a1, 0, 0, 0, 0, 0));
    case 33: /* dup2 */
        return linux_ret(posix_dup2(a1, a2, 0, 0, 0, 0));
    case 22: /* pipe */
        return linux_ret(posix_pipe(a1, 0, 0, 0, 0, 0));
    case 292: /* dup3 */
        return linux_ret(posix_dup3(a1, a2, a3, 0, 0, 0));
    case 39: /* getpid */
        return linux_ret(posix_getpid(0, 0, 0, 0, 0, 0));
    case 102: /* getuid */
        return linux_ret(posix_getuid(0, 0, 0, 0, 0, 0));
    case 104: /* getgid */
        return linux_ret(posix_getgid(0, 0, 0, 0, 0, 0));
    case 107: /* geteuid */
        return linux_ret(posix_geteuid(0, 0, 0, 0, 0, 0));
    case 108: /* getegid */
        return linux_ret(posix_getegid(0, 0, 0, 0, 0, 0));
    case 57: /* fork */
        return linux_ret(posix_fork(0, 0, 0, 0, 0, 0));
    case 56: /* clone (minimal: treat as fork) */
        return linux_ret(posix_fork(0, 0, 0, 0, 0, 0));
    case 58: /* vfork */
        return linux_ret(posix_fork(0, 0, 0, 0, 0, 0));
    case 59: /* execve */
        return linux_ret(posix_exec(a1, a2, a3, 0, 0, 0));
    case 60: /* exit */
        return linux_ret(posix_exit(a1, 0, 0, 0, 0, 0));
    case 62: /* kill */
        return linux_ret(posix_kill(a1, a2, 0, 0, 0, 0));
    case 61: /* wait4 */
    {
        uint64_t rc = posix_waitpid(a1, a2, 0, 0, 0, 0);
        if ((int64_t)rc == RDNX_E_NOTFOUND || (int64_t)rc == RDNX_E_DENIED) {
            return (uint64_t)(-LINUX_ECHILD);
        }
        return linux_ret(rc);
    }
    case 63: /* uname */
        return linux_ret(posix_uname(a1, 0, 0, 0, 0, 0));
    case 96: { /* gettimeofday */
        linux_timeval_t* tv = (linux_timeval_t*)(uintptr_t)a1;
        if (tv) {
            if (!unix_user_range_ok(tv, sizeof(*tv))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            uint64_t us = console_get_realtime_us();
            tv->tv_sec = (int64_t)(us / 1000000ULL);
            tv->tv_usec = (int64_t)(us % 1000000ULL);
        }
        /* timezone argument ignored */
        return 0;
    }
    case 164: /* settimeofday */
        return linux_ret(posix_settimeofday(a1, a2, 0, 0, 0, 0));
    case 99: { /* sysinfo */
        linux_sysinfo_u_t* out = (linux_sysinfo_u_t*)(uintptr_t)a1;
        if (!out || !unix_user_range_ok(out, sizeof(*out))) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        memset(out, 0, sizeof(*out));
        out->uptime = (int64_t)(console_get_uptime_us() / 1000000ULL);
        out->totalram = pmm_get_total_pages() * LINUX_PAGE_SIZE;
        out->freeram = pmm_get_free_pages() * LINUX_PAGE_SIZE;
        out->mem_unit = 1;
        return 0;
    }
    case 72: /* fcntl */
        return linux_ret(posix_fcntl(a1, a2, a3, 0, 0, 0));
    case 79: { /* getcwd */
        uint64_t rc = posix_getcwd(a1, a2, 0, 0, 0, 0);
        if ((int64_t)rc < 0) {
            return linux_ret(rc);
        }
        if (a1 == 0 || a2 == 0) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (!unix_user_range_ok((const void*)(uintptr_t)a1, (size_t)a2)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        const char* buf = (const char*)(uintptr_t)a1;
        size_t n = strlen(buf) + 1u; /* Guest ABI returns the length including the NUL byte. */
        return (uint64_t)n;
    }
    case 80: /* chdir */
        return linux_ret(posix_chdir(a1, 0, 0, 0, 0, 0));
    case 81: { /* fchdir */
        task_t* t = task_get_current();
        int fd = (int)a1;
        if (!t || fd < 0 || fd >= PROC_MAX_FD) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        /* было: task_proc(t)->fd_kind[fd] != UNIX_FD_KIND_VFS */
        rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(t), fd);
        if (!rf || rf->kind != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)rf->priv;
        if (!f || !f->node || f->node->type != VFS_NODE_DIR) {
            return (uint64_t)(-LINUX_ENOTDIR);
        }
        char abs[UNIX_PATH_MAX];
        if (linux_vfs_node_to_abspath(f->node, abs, sizeof(abs)) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        proc_t* proc = task_proc(t);
        strncpy(proc->cwd, abs, sizeof(proc->cwd) - 1);
        proc->cwd[sizeof(proc->cwd) - 1] = '\0';
        return 0;
    }
    case 82: { /* rename */
        uint64_t rc = linux_ret(posix_rename(a1, a2, 0, 0, 0, 0));
        if ((int64_t)rc >= 0) {
            const char* oldp = (const char*)(uintptr_t)a1;
            const char* newp = (const char*)(uintptr_t)a2;
            linux_symlink_rename_path(oldp, newp);
            linux_mode_rename_path(oldp, newp);
        }
        return rc;
    }
    case 83: { /* mkdir */
        task_t* t = task_get_current();
        uint64_t rc = linux_ret(posix_mkdir(a1, 0, 0, 0, 0, 0));
        if ((int64_t)rc >= 0 && t) {
            char pbuf[UNIX_PATH_MAX];
            if (unix_copy_user_cstr(pbuf, sizeof(pbuf), (const char*)(uintptr_t)a1) == RDNX_OK) {
                linux_mode_set(pbuf, (uint16_t)(a2 & ~(uint64_t)(task_proc(t)->umask & 0777u)));
            }
        }
        return rc;
    }
    case 84: /* rmdir */
        return linux_ret(posix_rmdir(a1, 0, 0, 0, 0, 0));
    case 85: /* creat */
    {
        task_t* t = task_get_current();
        uint64_t rc = linux_ret(posix_open(a1,
                                           (uint64_t)(VFS_OPEN_WRITE | VFS_OPEN_CREATE | VFS_OPEN_TRUNC),
                                           0, 0, 0, 0));
        if ((int64_t)rc >= 0 && t) {
            char pbuf[UNIX_PATH_MAX];
            if (unix_copy_user_cstr(pbuf, sizeof(pbuf), (const char*)(uintptr_t)a1) == RDNX_OK) {
                linux_mode_set(pbuf, (uint16_t)(a2 & ~(uint64_t)(task_proc(t)->umask & 0777u)));
            }
        }
        return rc;
    }
    case 86: { /* link */
        char old_path[UNIX_PATH_MAX];
        char new_path[UNIX_PATH_MAX];
        vfs_stat_t st_new;
        int old_sidx;
        if (unix_copy_user_cstr(old_path, sizeof(old_path), (const char*)(uintptr_t)a1) != RDNX_OK ||
            unix_copy_user_cstr(new_path, sizeof(new_path), (const char*)(uintptr_t)a2) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (vfs_stat(new_path, &st_new) == RDNX_OK || linux_symlink_find(new_path) >= 0) {
            return (uint64_t)(-LINUX_EEXIST);
        }
        old_sidx = linux_symlink_find(old_path);
        if (old_sidx >= 0) {
            int rc = linux_symlink_add(new_path, g_linux_symlinks[old_sidx].target);
            return (rc == RDNX_OK) ? 0ull : (uint64_t)(-LINUX_EIO);
        }
        vfs_file_t src;
        vfs_file_t dst;
        uint8_t buf[512];
        int rc = vfs_open(old_path, VFS_OPEN_READ, &src);
        if (rc != RDNX_OK) {
            return (uint64_t)(-LINUX_ENOENT);
        }
        rc = vfs_open(new_path, VFS_OPEN_WRITE | VFS_OPEN_CREATE | VFS_OPEN_TRUNC, &dst);
        if (rc != RDNX_OK) {
            (void)vfs_close(&src);
            return (uint64_t)(-LINUX_EIO);
        }
        for (;;) {
            int n = vfs_read(&src, buf, sizeof(buf));
            if (n < 0) {
                (void)vfs_close(&src);
                (void)vfs_close(&dst);
                return (uint64_t)(-LINUX_EIO);
            }
            if (n == 0) {
                break;
            }
            int wr = vfs_write(&dst, buf, (size_t)n);
            if (wr != n) {
                (void)vfs_close(&src);
                (void)vfs_close(&dst);
                return (uint64_t)(-LINUX_EIO);
            }
        }
        (void)vfs_close(&src);
        (void)vfs_close(&dst);
        return 0;
    }
    case 87: {
        const char* path = (const char*)(uintptr_t)a1;
        if (path && linux_symlink_remove(path) == RDNX_OK) {
            linux_mode_remove(path);
            return 0;
        }
        linux_mode_remove(path);
        return linux_ret(posix_unlink(a1, 0, 0, 0, 0, 0));
    }
    case 88: { /* symlink */
        char target[UNIX_PATH_MAX];
        char link_path[UNIX_PATH_MAX];
        vfs_stat_t st;
        if (unix_copy_user_cstr(target, sizeof(target), (const char*)(uintptr_t)a1) != RDNX_OK ||
            unix_copy_user_cstr(link_path, sizeof(link_path), (const char*)(uintptr_t)a2) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (vfs_stat(link_path, &st) == RDNX_OK || linux_symlink_find(link_path) >= 0) {
            return (uint64_t)(-LINUX_EEXIST);
        }
        {
            int rc = linux_symlink_add(link_path, target);
            if (rc != RDNX_OK) {
                return (uint64_t)(-LINUX_EIO);
            }
        }
        return 0;
    }
    case 89: { /* readlink */
        char path_buf[UNIX_PATH_MAX];
        char proc_target[UNIX_PATH_MAX];
        char* out = (char*)(uintptr_t)a2;
        uint64_t out_len = a3;
        if (unix_copy_user_cstr(path_buf, sizeof(path_buf), (const char*)(uintptr_t)a1) != RDNX_OK ||
            !out || out_len == 0 || !unix_user_range_ok(out, (size_t)out_len)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        const char* src = NULL;
        if (strcmp(path_buf, "/proc/self/exe") == 0) {
            src = "/bin/sh";
        } else if (linux_proc_fd_target(path_buf, proc_target, sizeof(proc_target)) == RDNX_OK) {
            src = proc_target;
        } else {
            int sidx = linux_symlink_find(path_buf);
            if (sidx >= 0) {
                src = g_linux_symlinks[sidx].target;
            }
        }
        if (!src) {
            return (uint64_t)(-LINUX_ENOENT);
        }
        size_t n = strlen(src);
        if (n > (size_t)out_len) {
            n = (size_t)out_len;
        }
        memcpy(out, src, n);
        return (uint64_t)n;
    }
    case 90: { /* chmod */
        char path_buf[UNIX_PATH_MAX];
        int rc;
        if (unix_copy_user_cstr(path_buf, sizeof(path_buf), (const char*)(uintptr_t)a1) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        linux_mode_set(path_buf, (uint16_t)a2);
        rc = (int)posix_chmod(a1, a2, 0, 0, 0, 0);
        return (rc < 0) ? (uint64_t)(-linux_errno_from_rdnx(rc)) : 0;
    }
    case 95: { /* umask */
        task_t* t = task_get_current();
        if (!t) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        proc_t* proc = task_proc(t);
        uint32_t old = proc->umask & 0777u;
        proc->umask = (uint16_t)(a1 & 0777u);
        return (uint64_t)old;
    }
    case 105: { /* setuid */
        int rc = (int)posix_setuid(a1, 0, 0, 0, 0, 0);
        return (rc < 0) ? (uint64_t)(-linux_errno_from_rdnx(rc)) : 0;
    }
    case 106: { /* setgid */
        int rc = (int)posix_setgid(a1, 0, 0, 0, 0, 0);
        return (rc < 0) ? (uint64_t)(-linux_errno_from_rdnx(rc)) : 0;
    }
    case 109: { /* setpgid(pid, pgid) */
        task_t* cur = task_get_current();
        uint64_t pid = a1;
        uint64_t pgid = a2;
        task_t* target = NULL;
        if (!cur) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (pid == 0) {
            target = cur;
        } else {
            target = task_find_by_id(pid);
            if (!target) {
                return (uint64_t)(-LINUX_ESRCH);
            }
        }
        if (target != cur && target->parent_task_id != cur->task_id) {
            return (uint64_t)(-LINUX_ESRCH);
        }
        if (pgid == 0) {
            pgid = target->task_id;
        }
        proc_t* tproc = task_proc(target);
        proc_t* cproc = task_proc(cur);
        if (!tproc || !cproc) {
            return (uint64_t)(-LINUX_ESRCH);
        }
        tproc->process_group_id = pgid;
        if (tproc->session_id == 0) {
            tproc->session_id = cproc->session_id ? cproc->session_id : cur->task_id;
        }
        if (tty_console_get_fg_pgrp() == 0 && target == cur) {
            tty_console_set_fg_pgrp(tproc->process_group_id);
        }
        return 0;
    }
    case 111: /* getpgrp */
    case 121: { /* getpgid */
        task_t* _t = NULL;
        if (a1 == 0) {
            _t = task_get_current();
        } else {
            _t = task_find_by_id(a1);
        }
        if (!_t) {
            return (uint64_t)(-LINUX_ESRCH);
        }
        proc_t* proc = task_proc(_t);
        return (proc && proc->process_group_id) ? proc->process_group_id : _t->task_id;
    }
    case 112: { /* setsid */
        task_t* _t = task_get_current();
        if (!_t) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        proc_t* proc = task_proc(_t);
        if (!proc) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        proc->session_id = _t->task_id;
        proc->process_group_id = _t->task_id;
        tty_console_set_fg_pgrp(proc->process_group_id);
        return proc->session_id;
    }
    case 113: { /* setpgid compat / setpgrp — same as 109 */
        task_t* cur = task_get_current();
        if (!cur) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        proc_t* proc = task_proc(cur);
        if (!proc) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        proc->process_group_id = cur->task_id;
        if (proc->session_id == 0) {
            proc->session_id = cur->task_id;
        }
        if (tty_console_get_fg_pgrp() == 0) {
            tty_console_set_fg_pgrp(proc->process_group_id);
        }
        return 0;
    }
    case 124: { /* getsid */
        task_t* t = NULL;
        if (a1 == 0) {
            t = task_get_current();
        } else {
            t = task_find_by_id(a1);
        }
        if (!t) {
            return (uint64_t)(-LINUX_ESRCH);
        }
        proc_t* proc = task_proc(t);
        return (proc && proc->session_id) ? proc->session_id : t->task_id;
    }
    case 110: { /* getppid */
        task_t* t = task_get_current();
        if (!t) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        return (uint64_t)t->parent_task_id;
    }
    case 158: { /* arch_prctl */
        task_t* task = task_get_current();
        if (!task) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (a1 == LINUX_ARCH_SET_FS) {
            task->tls_fs_base = a2;
            return 0;
        }
        if (a1 == LINUX_ARCH_GET_FS) {
            uint64_t* out = (uint64_t*)(uintptr_t)a2;
            if (!out || !unix_user_range_ok(out, sizeof(*out))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            *out = task->tls_fs_base;
            return 0;
        }
        return (uint64_t)(-LINUX_ENOSYS);
    }
    case 170: /* sethostname */
        return linux_ret(posix_sethostname(a1, a2, 0, 0, 0, 0));
    case 186: /* gettid */
    {
        return linux_ret(posix_getpid(0, 0, 0, 0, 0, 0));
    }
    case 218: /* set_tid_address */
        /* Minimal compatibility: accept pointer and return caller tid. */
        return linux_ret(posix_getpid(0, 0, 0, 0, 0, 0));
    case 228: /* clock_gettime */
        return linux_ret(posix_clock_gettime(a1, a2, 0, 0, 0, 0));
    case 231: /* exit_group */
        return linux_ret(posix_exit(a1, 0, 0, 0, 0, 0));
    case 257: /* openat */
        if ((int)a1 != LINUX_AT_FDCWD) {
            return (uint64_t)(-LINUX_ENOSYS);
        }
        return linux_compat_dispatch(2, a2, a3, a4, 0, 0, 0);
    case 262: /* newfstatat */
    {
        /* a1=dirfd, a2=path, a3=statbuf, a4=flags */
        if ((int)a1 != LINUX_AT_FDCWD) {
            return (uint64_t)(-LINUX_ENOSYS);
        }
        char _fsa_path[UNIX_PATH_MAX];
        vfs_stat_t _fsa_st;
        linux_stat_u_t* _fsa_ust = (linux_stat_u_t*)(uintptr_t)a3;
        if (!_fsa_ust || !unix_user_range_ok(_fsa_ust, sizeof(*_fsa_ust))) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (unix_resolve_user_path((const char*)(uintptr_t)a2,
                                   _fsa_path, sizeof(_fsa_path)) != RDNX_OK) {
            return (uint64_t)(-LINUX_ENOENT);
        }
        int _fsa_rc = vfs_stat(_fsa_path, &_fsa_st);
        if (_fsa_rc != RDNX_OK) {
            return linux_ret((uint64_t)_fsa_rc);
        }
        linux_stat_u_t _fsa_kst;
        memset(&_fsa_kst, 0, sizeof(_fsa_kst));
        _fsa_kst.st_dev     = 1;
        _fsa_kst.st_ino     = 1;
        _fsa_kst.st_nlink   = 1;
        _fsa_kst.st_mode    = _fsa_st.mode;
        _fsa_kst.st_uid     = _fsa_st.uid;
        _fsa_kst.st_gid     = _fsa_st.gid;
        _fsa_kst.st_size    = (int64_t)_fsa_st.size;
        _fsa_kst.st_blksize = 4096;
        _fsa_kst.st_blocks  = (int64_t)((_fsa_st.size + 511) / 512);
        _fsa_kst.st_mtim_sec = (int64_t)_fsa_st.mtime;
        _fsa_kst.st_atim_sec = (int64_t)_fsa_st.mtime;
        _fsa_kst.st_ctim_sec = (int64_t)_fsa_st.mtime;
        if (unix_copy_to_user(_fsa_ust, &_fsa_kst, sizeof(_fsa_kst)) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        return 0;
    }
    case 269: { /* faccessat */
        int flags = (int)a4;
        if ((int)a1 != LINUX_AT_FDCWD) {
            return (uint64_t)(-LINUX_ENOSYS);
        }
        if ((flags & ~(LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_EACCESS | LINUX_AT_EMPTY_PATH)) != 0) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        /* Symlink/eaccess/empty-path semantics are currently accepted but not distinguished. */
        return linux_compat_dispatch(21, a2, a3, 0, 0, 0, 0);
    }
    case 293: /* pipe2 */
        return linux_ret(posix_pipe2(a1, a2, 0, 0, 0, 0));
    case 17: { /* pread64 */
        task_t* t = task_get_current();
        int fd = (int)a1;
        if (!t || fd < 0 || fd >= PROC_MAX_FD) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        /* было: task_proc(t)->fd_kind[fd] != UNIX_FD_KIND_VFS */
        rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(t), fd);
        if (!rf || rf->kind != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)rf->priv;
        if (!f) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        size_t old = f->pos;
        f->pos = (size_t)a4;
        uint64_t rc = linux_compat_dispatch(0, a1, a2, a3, 0, 0, 0);
        f->pos = old;
        return rc;
    }
    case 18: { /* pwrite64 */
        task_t* t = task_get_current();
        int fd = (int)a1;
        if (!t || fd < 0 || fd >= PROC_MAX_FD) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        /* было: task_proc(t)->fd_kind[fd] != UNIX_FD_KIND_VFS */
        rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(t), fd);
        if (!rf || rf->kind != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)rf->priv;
        if (!f) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        size_t old = f->pos;
        f->pos = (size_t)a4;
        uint64_t rc = linux_compat_dispatch(1, a1, a2, a3, 0, 0, 0);
        f->pos = old;
        return rc;
    }
    case 19: { /* readv */
        linux_iovec_u_t* iov = (linux_iovec_u_t*)(uintptr_t)a2;
        uint64_t iovcnt = a3;
        uint64_t total = 0;
        if (!iov || iovcnt > 64 || !unix_user_range_ok(iov, (size_t)(iovcnt * sizeof(*iov)))) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        for (uint64_t i = 0; i < iovcnt; i++) {
            uint64_t r = linux_compat_dispatch(0, a1, iov[i].iov_base, iov[i].iov_len, 0, 0, 0);
            if ((int64_t)r < 0) {
                return (total > 0) ? total : r;
            }
            total += r;
            if (r < iov[i].iov_len) {
                break;
            }
        }
        return total;
    }
    case 20: { /* writev */
        linux_iovec_u_t* iov = (linux_iovec_u_t*)(uintptr_t)a2;
        uint64_t iovcnt = a3;
        uint64_t total = 0;
        if (!iov || iovcnt > 64 || !unix_user_range_ok(iov, (size_t)(iovcnt * sizeof(*iov)))) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        for (uint64_t i = 0; i < iovcnt; i++) {
            uint64_t r = linux_compat_dispatch(1, a1, iov[i].iov_base, iov[i].iov_len, 0, 0, 0);
            if ((int64_t)r < 0) {
                return (total > 0) ? total : r;
            }
            total += r;
            if (r < iov[i].iov_len) {
                break;
            }
        }
        return total;
    }
    case 74: /* fsync */
    case 75: /* fdatasync */
        return 0;
    case 78: { /* getdents (legacy linux_dirent) */
        task_t* t = task_get_current();
        int fd = (int)a1;
        uint8_t* out = (uint8_t*)(uintptr_t)a2;
        uint64_t out_len = a3;
        if (!t || fd < 0 || fd >= PROC_MAX_FD || !out || out_len < sizeof(linux_dirent_u_t)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        /* было: task_proc(t)->fd_kind[fd] != UNIX_FD_KIND_VFS */
        rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(t), fd);
        if (!rf || rf->kind != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)rf->priv;
        if (!f || !f->node || f->node->type != VFS_NODE_DIR) {
            return (uint64_t)(-LINUX_ENOTDIR);
        }
        if (!unix_user_range_ok(out, (size_t)out_len)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        uint64_t wrote = 0;
        uint64_t idx = 0;
        for (vfs_node_t* ch = f->node->children; ch; ch = ch->sibling, idx++) {
            if (idx < f->pos) {
                continue;
            }
            size_t nlen = strlen(ch->name);
            size_t reclen = sizeof(linux_dirent_u_t) + nlen + 2; /* +NUL +dtype slot */
            reclen = (reclen + 7u) & ~7u;
            if (wrote + reclen > out_len) {
                break;
            }
            linux_dirent_u_t* d = (linux_dirent_u_t*)(out + wrote);
            memset(d, 0, reclen);
            d->d_ino = idx + 1;
            d->d_off = idx + 1;
            d->d_reclen = (uint16_t)reclen;
            memcpy(d->d_name, ch->name, nlen + 1);
            out[wrote + reclen - 1] = (ch->type == VFS_NODE_DIR) ? LINUX_DT_DIR : LINUX_DT_REG;
            wrote += reclen;
            f->pos = idx + 1;
        }
        return wrote;
    }
    case 217: { /* getdents64 */
        task_t* t = task_get_current();
        int fd = (int)a1;
        uint8_t* out = (uint8_t*)(uintptr_t)a2;
        uint64_t out_len = a3;
        if (!t || fd < 0 || fd >= PROC_MAX_FD || !out || out_len < sizeof(linux_dirent64_u_t)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        /* было: task_proc(t)->fd_kind[fd] != UNIX_FD_KIND_VFS */
        rdnx_file_t* rf = (rdnx_file_t*)proc_fd_get(task_proc(t), fd);
        if (!rf || rf->kind != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)rf->priv;
        if (!f || !f->node || f->node->type != VFS_NODE_DIR) {
            return (uint64_t)(-LINUX_ENOTDIR);
        }
        if (!unix_user_range_ok(out, (size_t)out_len)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        uint64_t wrote = 0;
        uint64_t idx = 0;
        for (vfs_node_t* ch = f->node->children; ch; ch = ch->sibling, idx++) {
            if (idx < f->pos) {
                continue;
            }
            size_t nlen = strlen(ch->name);
            size_t reclen = sizeof(linux_dirent64_u_t) + nlen + 1;
            reclen = (reclen + 7u) & ~7u;
            if (wrote + reclen > out_len) {
                break;
            }
            linux_dirent64_u_t* d = (linux_dirent64_u_t*)(out + wrote);
            memset(d, 0, reclen);
            d->d_ino = idx + 1;
            d->d_off = (int64_t)(idx + 1);
            d->d_reclen = (uint16_t)reclen;
            d->d_type = (ch->type == VFS_NODE_DIR) ? LINUX_DT_DIR : LINUX_DT_REG;
            memcpy(d->d_name, ch->name, nlen + 1);
            wrote += reclen;
            f->pos = idx + 1;
        }
        return wrote;
    }
    case 273: /* set_robust_list */
        /* Not implemented yet; keep startup paths alive. */
        return 0;
    default:
        return (uint64_t)(-LINUX_ENOSYS);
    }
}
