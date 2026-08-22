/**
 * @file procfs.c
 * @brief Process filesystem — virtual /proc implementation
 *
 * Exposes kernel state as a read-only virtual filesystem mounted at /proc.
 * Files are generated dynamically on each read() via vfs_inode_t.read_fn.
 */

#include "procfs.h"
#include "vfs.h"
#include "../../include/common.h"
#include "../../include/error.h"
#include "../../include/console.h"
#include "../../include/version.h"
#include "../lib/heap.h"
#include "../kernel/core/task.h"
#include "../kernel/unix/proc.h"
#include "../kernel/arch/x86_64/pmm.h"

/* ============================================================================
 * Minimal buffer helper for generating procfs file content
 * ============================================================================ */

typedef struct {
    char*  ptr;
    size_t cap;
    size_t len;
} pbuf_t;

static void pb_char(pbuf_t* p, char c)
{
    if (p->len < p->cap) {
        p->ptr[p->len++] = c;
    }
}

static void pb_str(pbuf_t* p, const char* s)
{
    while (s && *s) {
        pb_char(p, *s++);
    }
}

static void pb_u64(pbuf_t* p, uint64_t v)
{
    char tmp[21];
    int i = 20;
    tmp[20] = '\0';
    if (v == 0) {
        pb_char(p, '0');
        return;
    }
    while (v > 0 && i > 0) {
        tmp[--i] = '0' + (int)(v % 10);
        v /= 10;
    }
    pb_str(p, tmp + i);
}

static void pb_kv_str(pbuf_t* p, const char* key, const char* val)
{
    pb_str(p, key);
    pb_str(p, ":\t");
    pb_str(p, val);
    pb_char(p, '\n');
}

static void pb_kv_u64(pbuf_t* p, const char* key, uint64_t val)
{
    pb_str(p, key);
    pb_str(p, ":\t");
    pb_u64(p, val);
    pb_char(p, '\n');
}

static void pb_kv_kb(pbuf_t* p, const char* key, uint64_t pages)
{
    pb_str(p, key);
    pb_str(p, ":\t");
    pb_u64(p, pages * 4); /* 4096-byte pages → kB */
    pb_str(p, " kB\n");
}

/* Serve bytes from a generated buffer to userspace, tracking file->pos */
static int procfs_serve(vfs_file_t* file, const char* buf, size_t len,
                        void* out, size_t out_size)
{
    if (!buf || file->pos >= len) {
        return 0; /* EOF */
    }
    size_t avail = len - file->pos;
    size_t n = avail < out_size ? avail : out_size;
    memcpy(out, buf + file->pos, n);
    file->pos += n;
    return (int)n;
}

/* ============================================================================
 * /proc/version
 * ============================================================================ */

static int procfs_read_version(vfs_file_t* file, void* buf, size_t size)
{
    char tmp[256];
    pbuf_t p = { tmp, sizeof(tmp) - 1, 0 };

    pb_str(&p, RODNIX_SYSNAME " version " RODNIX_RELEASE " (" RODNIX_MACHINE ") "
               RODNIX_VERSION "\n");
    tmp[p.len] = '\0';

    return procfs_serve(file, tmp, p.len, buf, size);
}

/* ============================================================================
 * /proc/meminfo
 * ============================================================================ */

static int procfs_read_meminfo(vfs_file_t* file, void* buf, size_t size)
{
    char tmp[512];
    pbuf_t p = { tmp, sizeof(tmp) - 1, 0 };

    uint64_t total = pmm_get_total_pages();
    uint64_t free  = pmm_get_free_pages();
    uint64_t used  = pmm_get_used_pages();

    pb_kv_kb(&p, "MemTotal",     total);
    pb_kv_kb(&p, "MemFree",      free);
    pb_kv_kb(&p, "MemAvailable", free);
    pb_kv_kb(&p, "MemUsed",      used);
    tmp[p.len] = '\0';

    return procfs_serve(file, tmp, p.len, buf, size);
}

/* ============================================================================
 * /proc/uptime
 * ============================================================================ */

static int procfs_read_uptime(vfs_file_t* file, void* buf, size_t size)
{
    char tmp[64];
    pbuf_t p = { tmp, sizeof(tmp) - 1, 0 };

    uint64_t us = console_get_uptime_us();
    uint64_t secs = us / 1000000ULL;
    uint64_t cs   = (us % 1000000ULL) / 10000ULL; /* centiseconds */

    pb_u64(&p, secs);
    pb_char(&p, '.');
    if (cs < 10) {
        pb_char(&p, '0');
    }
    pb_u64(&p, cs);
    pb_str(&p, " 0.00\n");
    tmp[p.len] = '\0';

    return procfs_serve(file, tmp, p.len, buf, size);
}

/* ============================================================================
 * /proc/self/status  (current task)
 * ============================================================================ */

static const char* task_state_name(task_state_t s)
{
    switch (s) {
        case TASK_STATE_RUNNING:  return "R (running)";
        case TASK_STATE_READY:    return "R (runnable)";
        case TASK_STATE_SLEEPING: return "S (sleeping)";
        case TASK_STATE_BLOCKED:  return "D (blocked)";
        case TASK_STATE_ZOMBIE:   return "Z (zombie)";
        default:                  return "X (dead)";
    }
}

static char task_state_char(task_state_t s)
{
    switch (s) {
        case TASK_STATE_RUNNING:  return 'R';
        case TASK_STATE_READY:    return 'R';
        case TASK_STATE_SLEEPING: return 'S';
        case TASK_STATE_BLOCKED:  return 'D';
        case TASK_STATE_ZOMBIE:   return 'Z';
        default:                  return 'X';
    }
}

/* Kernel-задачи не обязаны иметь UNIX-персоналию: читаем её безопасно. */
static uint32_t proc_uid_or_zero(const task_t* t)
{
    const proc_t* pr = task_proc(t);
    return pr ? pr->uid : 0u;
}

static const char* proc_cwd_or(const task_t* t, const char* fallback)
{
    const proc_t* pr = task_proc(t);
    return (pr && pr->cwd[0]) ? pr->cwd : fallback;
}

static void pb_task_status(pbuf_t* p, const task_t* t)
{
    const proc_t* pr = task_proc(t);
    pb_kv_str(p, "Name",  (pr && pr->cwd[0]) ? pr->cwd : "kernel");
    pb_kv_str(p, "State", task_state_name(t->state));
    pb_kv_u64(p, "Pid",   t->task_id);
    pb_kv_u64(p, "PPid",  t->parent_task_id);
    pb_kv_u64(p, "Threads", t->thread_count);
    /* Uid: real effective saved fs */
    pb_str(p, "Uid:\t");
    pb_u64(p, pr ? pr->uid : 0);  pb_char(p, '\t');
    pb_u64(p, proc_get_euid(pr)); pb_char(p, '\t');
    pb_u64(p, proc_get_euid(pr)); pb_char(p, '\t');
    pb_u64(p, proc_get_euid(pr)); pb_char(p, '\n');
    /* Gid: real effective saved fs */
    pb_str(p, "Gid:\t");
    pb_u64(p, pr ? pr->gid : 0);  pb_char(p, '\t');
    pb_u64(p, proc_get_egid(pr)); pb_char(p, '\t');
    pb_u64(p, proc_get_egid(pr)); pb_char(p, '\t');
    pb_u64(p, proc_get_egid(pr)); pb_char(p, '\n');
}

static int procfs_read_self_status(vfs_file_t* file, void* buf, size_t size)
{
    char tmp[512];
    pbuf_t p = { tmp, sizeof(tmp) - 1, 0 };

    task_t* t = task_get_current();
    if (t) {
        pb_task_status(&p, t);
    } else {
        pb_str(&p, "Name:\tkernel\nState:\tR (running)\nPid:\t0\nPPid:\t0\n");
    }
    tmp[p.len] = '\0';

    return procfs_serve(file, tmp, p.len, buf, size);
}

/* ============================================================================
 * /proc/<pid>/status  (specific task by PID stored in inode->fs_aux)
 * ============================================================================ */

static int procfs_read_pid_status(vfs_file_t* file, void* buf, size_t size)
{
    char tmp[512];
    pbuf_t p = { tmp, sizeof(tmp) - 1, 0 };

    uint64_t pid = (uint64_t)file->node->inode->fs_aux;
    task_t* t = task_find_by_id(pid);
    if (t) {
        pb_task_status(&p, t);
    } else {
        pb_str(&p, "Name:\t(exited)\nState:\tX (dead)\n");
        pb_kv_u64(&p, "Pid", pid);
    }
    tmp[p.len] = '\0';

    return procfs_serve(file, tmp, p.len, buf, size);
}

/* ============================================================================
 * /proc/status  — one-shot listing of all tasks (like ps)
 * ============================================================================ */

typedef struct {
    char*  buf;
    size_t cap;
    size_t len;
} procfs_allbuf_t;

static void procfs_append_task(const task_t* t, void* ctx)
{
    procfs_allbuf_t* ab = (procfs_allbuf_t*)ctx;
    if (ab->len >= ab->cap) {
        return;
    }
    pbuf_t p = { ab->buf + ab->len, ab->cap - ab->len - 1, 0 };

    /* Format: PID\tPPID\tSTAT\tNTHRD\tUID\tCPUTICKS\tCOMMAND\n */
    pb_u64(&p, t->task_id);
    pb_char(&p, '\t');
    pb_u64(&p, t->parent_task_id);
    pb_char(&p, '\t');
    pb_char(&p, task_state_char(t->state));
    pb_char(&p, '\t');
    pb_u64(&p, t->thread_count);
    pb_char(&p, '\t');
    pb_u64(&p, proc_uid_or_zero(t));
    pb_char(&p, '\t');
    pb_u64(&p, t->thread_group.cpu_ticks);
    pb_char(&p, '\t');
    pb_str(&p, proc_cwd_or(t, "/"));
    pb_char(&p, '\n');

    ab->len += p.len;
}

static int procfs_read_allstatus(vfs_file_t* file, void* buf, size_t size)
{
    /* Generate on first read (pos == 0) only; cache in inode->data */
    vfs_inode_t* inode = file->node->inode;

    if (file->pos == 0) {
        /* (Re)generate snapshot */
        if (inode->data) {
            kfree(inode->data);
            inode->data = NULL;
            inode->size = 0;
            inode->capacity = 0;
        }
        const size_t snap_cap = 4096;
        char* snap = (char*)kmalloc(snap_cap);
        if (!snap) {
            return RDNX_E_NOMEM;
        }
        /* header */
        pbuf_t hdr = { snap, snap_cap - 1, 0 };
        pb_str(&hdr, "PID\tPPID\tSTAT\tNTHRD\tUID\tCPUTICKS\tCOMMAND\n");

        procfs_allbuf_t ab = { snap, snap_cap, hdr.len };
        task_for_each(procfs_append_task, &ab);
        ab.buf[ab.len] = '\0';

        inode->data     = (uint8_t*)snap;
        inode->size     = ab.len;
        inode->capacity = snap_cap;
    }

    if (!inode->data || file->pos >= inode->size) {
        return 0;
    }
    size_t avail = inode->size - file->pos;
    size_t n = avail < size ? avail : size;
    memcpy(buf, inode->data + file->pos, n);
    file->pos += n;
    return (int)n;
}

static int procfs_read_pid_stat(vfs_file_t* file, void* buf, size_t size)
{
    char tmp[256];
    pbuf_t p = { tmp, sizeof(tmp) - 1, 0 };

    uint64_t pid = (uint64_t)file->node->inode->fs_aux;
    task_t* t = task_find_by_id(pid);
    if (t) {
        /* pid (command) stat ppid uid nthrd cputicks */
        pb_u64(&p, t->task_id);
        pb_str(&p, " (");
        pb_str(&p, proc_cwd_or(t, "kernel"));
        pb_str(&p, ") ");
        pb_char(&p, task_state_char(t->state));
        pb_char(&p, ' ');
        pb_u64(&p, t->parent_task_id);
        pb_char(&p, ' ');
        pb_u64(&p, proc_uid_or_zero(t));
        pb_char(&p, ' ');
        pb_u64(&p, t->thread_count);
        pb_char(&p, ' ');
        pb_u64(&p, t->thread_group.cpu_ticks);
        pb_char(&p, '\n');
    } else {
        pb_u64(&p, pid);
        pb_str(&p, " (exited) X 0 0 0 0\n");
    }
    tmp[p.len] = '\0';

    return procfs_serve(file, tmp, p.len, buf, size);
}

static int procfs_read_self_stat(vfs_file_t* file, void* buf, size_t size)
{
    char tmp[256];
    pbuf_t p = { tmp, sizeof(tmp) - 1, 0 };

    task_t* t = task_get_current();
    if (t) {
        pb_u64(&p, t->task_id);
        pb_str(&p, " (");
        pb_str(&p, proc_cwd_or(t, "kernel"));
        pb_str(&p, ") ");
        pb_char(&p, task_state_char(t->state));
        pb_char(&p, ' ');
        pb_u64(&p, t->parent_task_id);
        pb_char(&p, ' ');
        pb_u64(&p, proc_uid_or_zero(t));
        pb_char(&p, ' ');
        pb_u64(&p, t->thread_count);
        pb_char(&p, ' ');
        pb_u64(&p, t->thread_group.cpu_ticks);
        pb_char(&p, '\n');
    } else {
        pb_str(&p, "0 (kernel) R 0 0 1 0\n");
    }
    tmp[p.len] = '\0';

    return procfs_serve(file, tmp, p.len, buf, size);
}

/* ============================================================================
 * Node creation helpers
 * ============================================================================ */

static vfs_node_t* procfs_make_file(vfs_node_t* dir, const char* name,
                                    vfs_read_fn_t rfn, uint32_t aux)
{
    vfs_node_t* node = vfs_fs_alloc_node(name, VFS_NODE_FILE);
    if (!node || !node->inode) {
        return NULL;
    }
    node->inode->flags   |= VFS_INODE_PROCFS;
    node->inode->fs_tag   = VFS_FS_TAG_PROCFS;
    node->inode->fs_aux   = aux;
    node->inode->read_fn  = rfn;
    node->inode->mode     = 0; /* world-readable; security check skipped for mode==0 */
    if (vfs_fs_add_child(dir, node) != RDNX_OK) {
        vfs_fs_free_node(node);
        return NULL;
    }
    return node;
}

static vfs_node_t* procfs_make_dir(vfs_node_t* parent, const char* name)
{
    vfs_node_t* dir = vfs_fs_alloc_node(name, VFS_NODE_DIR);
    if (!dir) {
        return NULL;
    }
    dir->inode->mode = 0;
    if (vfs_fs_add_child(parent, dir) != RDNX_OK) {
        vfs_fs_free_node(dir);
        return NULL;
    }
    return dir;
}

/* ============================================================================
 * Per-PID directory creation (snapshot at mount time)
 * ============================================================================ */

typedef struct {
    uint64_t pids[256];
    uint32_t count;
} procfs_pid_list_t;

static void procfs_collect_pid(const task_t* t, void* ctx)
{
    procfs_pid_list_t* pl = (procfs_pid_list_t*)ctx;
    if (pl->count < 256) {
        pl->pids[pl->count++] = t->task_id;
    }
}

/* Convert uint64_t to decimal string in buf (must be at least 21 bytes) */
static void u64_to_str(uint64_t v, char* buf)
{
    int i = 19;
    buf[20] = '\0';
    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (v > 0 && i >= 0) {
        buf[i--] = '0' + (int)(v % 10);
        v /= 10;
    }
    /* shift to front */
    int start = i + 1;
    int j = 0;
    while (start <= 19) {
        buf[j++] = buf[start++];
    }
    buf[j] = '\0';
}

static void procfs_create_pid_dir(vfs_node_t* root, uint64_t pid)
{
    char name[21];
    u64_to_str(pid, name);

    vfs_node_t* dir = procfs_make_dir(root, name);
    if (!dir) {
        return;
    }
    (void)procfs_make_file(dir, "status", procfs_read_pid_status, (uint32_t)pid);
    (void)procfs_make_file(dir, "stat", procfs_read_pid_stat, (uint32_t)pid);
}

/* ============================================================================
 * Mount function
 * ============================================================================ */

static int procfs_mount(const char* source, vfs_node_t** out_root)
{
    (void)source;
    if (!out_root) {
        return RDNX_E_INVALID;
    }

    vfs_node_t* root = vfs_fs_alloc_node("/", VFS_NODE_DIR);
    if (!root) {
        return RDNX_E_NOMEM;
    }
    root->inode->mode = 0;

    /* Global files */
    if (!procfs_make_file(root, "version", procfs_read_version,   0) ||
        !procfs_make_file(root, "meminfo", procfs_read_meminfo,   0) ||
        !procfs_make_file(root, "uptime",  procfs_read_uptime,    0) ||
        !procfs_make_file(root, "status",  procfs_read_allstatus, 0)) {
        return RDNX_E_NOMEM;
    }

    /* /proc/self/ */
    vfs_node_t* self = procfs_make_dir(root, "self");
    if (!self) {
        return RDNX_E_NOMEM;
    }
    if (!procfs_make_file(self, "status", procfs_read_self_status, 0)) {
        return RDNX_E_NOMEM;
    }
    if (!procfs_make_file(self, "stat", procfs_read_self_stat, 0)) {
        return RDNX_E_NOMEM;
    }

    /* Per-PID directories (snapshot of currently running tasks) */
    procfs_pid_list_t pl;
    pl.count = 0;
    task_for_each(procfs_collect_pid, &pl);
    for (uint32_t i = 0; i < pl.count; i++) {
        procfs_create_pid_dir(root, pl.pids[i]);
    }

    *out_root = root;
    return RDNX_OK;
}

/* ============================================================================
 * Driver registration
 * ============================================================================ */

static const vfs_fs_driver_t procfs_driver = {
    .name  = "procfs",
    .mount = procfs_mount,
};

int procfs_fs_init(void)
{
    return vfs_register_fs(&procfs_driver);
}
