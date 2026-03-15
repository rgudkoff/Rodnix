#include "linux_compat.h"
#include "linux_errno.h"
#include "../posix/posix_sys_file.h"
#include "../posix/posix_sys_ids.h"
#include "../posix/posix_sys_info.h"
#include "../posix/posix_sys_proc.h"
#include "../posix/posix_sys_vm.h"
#include "../core/task.h"
#include "../fs/vfs.h"
#include "../unix/unix_layer.h"
#include "../arch/x86_64/pmm.h"
#include "../vm/vm_map.h"
#include "../../include/error.h"
#include "../../include/common.h"
#include "../../include/console.h"

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
} linux_trace_entry_t;

enum {
    LINUX_TRACE_RING = 64
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
        kprintf("[LNXTRACE] #%llu n=%llu a1=%llx a2=%llx a3=%llx a4=%llx a5=%llx a6=%llx\n",
                (unsigned long long)g_linux_trace[i].seq,
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
        return linux_ret(posix_open(a1, (uint64_t)linux_to_rdnx_open_flags((int)a2), 0, 0, 0, 0));
    case 3:  /* close */
        return linux_ret(posix_close(a1, 0, 0, 0, 0, 0));
    case 4:  /* stat */
        return linux_ret(posix_stat(a1, a2, 0, 0, 0, 0));
    case 5:  /* fstat */
        return linux_ret(posix_fstat(a1, a2, 0, 0, 0, 0));
    case 6:  /* lstat (no symlink support yet) */
        return linux_ret(posix_stat(a1, a2, 0, 0, 0, 0));
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
            LINUX_TIOCGWINSZ = 0x5413u
        };
        if ((uint32_t)a2 == LINUX_TIOCGWINSZ) {
            linux_winsize_u_t* ws = (linux_winsize_u_t*)(uintptr_t)a3;
            if (!ws || !unix_user_range_ok(ws, sizeof(*ws))) {
                return (uint64_t)(-LINUX_EINVAL);
            }
            ws->ws_row = 25;
            ws->ws_col = 80;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
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
        return linux_ret(posix_waitpid(a1, a2, 0, 0, 0, 0));
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
        if (!t || fd < 0 || fd >= TASK_MAX_FD || t->fd_kind[fd] != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)task_fd_get(t, fd);
        if (!f || !f->node || f->node->type != VFS_NODE_DIR) {
            return (uint64_t)(-LINUX_ENOTDIR);
        }
        char abs[UNIX_PATH_MAX];
        if (linux_vfs_node_to_abspath(f->node, abs, sizeof(abs)) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        strncpy(t->cwd, abs, sizeof(t->cwd) - 1);
        t->cwd[sizeof(t->cwd) - 1] = '\0';
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
                linux_mode_set(pbuf, (uint16_t)(a2 & ~(uint64_t)(t->umask & 0777u)));
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
                linux_mode_set(pbuf, (uint16_t)(a2 & ~(uint64_t)(t->umask & 0777u)));
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
        char* out = (char*)(uintptr_t)a2;
        uint64_t out_len = a3;
        if (unix_copy_user_cstr(path_buf, sizeof(path_buf), (const char*)(uintptr_t)a1) != RDNX_OK ||
            !out || out_len == 0 || !unix_user_range_ok(out, (size_t)out_len)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        const char* src = NULL;
        if (strcmp(path_buf, "/proc/self/exe") == 0) {
            src = "/bin/sh";
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
        vfs_stat_t st;
        if (unix_copy_user_cstr(path_buf, sizeof(path_buf), (const char*)(uintptr_t)a1) != RDNX_OK) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (linux_symlink_find(path_buf) < 0 && vfs_stat(path_buf, &st) != RDNX_OK) {
            return (uint64_t)(-LINUX_ENOENT);
        }
        linux_mode_set(path_buf, (uint16_t)a2);
        return 0;
    }
    case 95: { /* umask */
        task_t* t = task_get_current();
        if (!t) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        uint32_t old = t->umask & 0777u;
        t->umask = (uint16_t)(a1 & 0777u);
        return (uint64_t)old;
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
    case 186: /* gettid */
        return linux_ret(posix_getpid(0, 0, 0, 0, 0, 0));
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
        return linux_ret(posix_open(a2, (uint64_t)linux_to_rdnx_open_flags((int)a3), 0, 0, 0, 0));
    case 262: /* newfstatat */
        if ((int)a1 != LINUX_AT_FDCWD) {
            return (uint64_t)(-LINUX_ENOSYS);
        }
        return linux_ret(posix_stat(a2, a3, 0, 0, 0, 0));
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
        if (!t || fd < 0 || fd >= TASK_MAX_FD || t->fd_kind[fd] != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)task_fd_get(t, fd);
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
        if (!t || fd < 0 || fd >= TASK_MAX_FD || t->fd_kind[fd] != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)task_fd_get(t, fd);
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
        if (!t || fd < 0 || fd >= TASK_MAX_FD || !out || out_len < sizeof(linux_dirent_u_t)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (t->fd_kind[fd] != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)task_fd_get(t, fd);
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
        if (!t || fd < 0 || fd >= TASK_MAX_FD || !out || out_len < sizeof(linux_dirent64_u_t)) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        if (t->fd_kind[fd] != UNIX_FD_KIND_VFS) {
            return (uint64_t)(-LINUX_EINVAL);
        }
        vfs_file_t* f = (vfs_file_t*)task_fd_get(t, fd);
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
