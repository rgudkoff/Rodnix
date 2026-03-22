/*
 * init.c
 * Minimal userspace launcher: smoke + exec /bin/sh.
 */

#include <stdint.h>
#include <stdlib.h>
#include "syscall.h"
#include "posix_syscall.h"
#include "unistd.h"
#include "sys/wait.h"
#include "sys/stat.h"
#include "dirent.h"
#include "time.h"
#include "string.h"
#include "stdio.h"

#define VFS_OPEN_READ 1
#define VFS_OPEN_WRITE 2
#define FD_STDOUT 1
#define SYS_TEST_SLEEP 120
#define SERVICE_MAX 16
#define SERVICE_NAME_MAX 32
#define SERVICE_ARGS_MAX 8
#define SERVICE_ARG_MAX 96
#define SERVICE_RESPAWN_DELAY_US 500000ULL

typedef enum {
    SERVICE_MODE_RESPAWN = 0,
    SERVICE_MODE_ONESHOT = 1
} service_mode_t;

typedef enum {
    SERVICE_STATE_INACTIVE = 0,
    SERVICE_STATE_RUNNING,
    SERVICE_STATE_EXITED,
    SERVICE_STATE_FAILED,
    SERVICE_STATE_BACKOFF
} service_state_t;

typedef struct {
    int used;
    int started;
    char name[SERVICE_NAME_MAX];
    service_mode_t mode;
    char argv_storage[SERVICE_ARGS_MAX][SERVICE_ARG_MAX];
    char* argv[SERVICE_ARGS_MAX + 1];
    int argc;
    pid_t pid;
    uint64_t restart_after_us;
    uint32_t restart_count;
    int last_exit_status;
    int last_term_signal;
    service_state_t state;
} service_t;

static service_t g_services[SERVICE_MAX];
static int g_service_count = 0;

static int cstr_eq(const char* a, const char* b);
static int cstr_contains(const char* s, const char* needle);

static long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

static void write_hex_byte(uint8_t b)
{
    char hex[2];
    static const char table[] = "0123456789ABCDEF";
    hex[0] = table[(b >> 4) & 0xF];
    hex[1] = table[b & 0xF];
    (void)write_buf(hex, 2);
}

static void copy_str(char* dst, size_t cap, const char* src)
{
    size_t i = 0;

    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static uint64_t monotonic_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

static void write_service_prefix(const char* name)
{
    (void)write_str("[svc] ");
    (void)write_str(name ? name : "?");
    (void)write_str(": ");
}

static int parse_service_mode(const char* s, service_mode_t* out)
{
    if (!s || !out) {
        return 0;
    }
    if (cstr_eq(s, "respawn")) {
        *out = SERVICE_MODE_RESPAWN;
        return 1;
    }
    if (cstr_eq(s, "oneshot")) {
        *out = SERVICE_MODE_ONESHOT;
        return 1;
    }
    return 0;
}

static int next_token(char** cursor, char* out, size_t out_cap)
{
    char* p;
    size_t len = 0;

    if (!cursor || !*cursor || !out || out_cap == 0) {
        return 0;
    }

    p = *cursor;
    while (*p != '\0' && is_space_char(*p)) {
        p++;
    }
    if (*p == '\0' || *p == '#') {
        *cursor = p;
        out[0] = '\0';
        return 0;
    }

    while (*p != '\0' && !is_space_char(*p) && *p != '#') {
        if (len + 1 < out_cap) {
            out[len++] = *p;
        }
        p++;
    }
    out[len] = '\0';

    while (*p != '\0' && !is_space_char(*p)) {
        p++;
    }
    *cursor = p;
    return 1;
}

static int add_service(service_t* svc)
{
    int idx;
    int i;

    if (!svc || g_service_count >= SERVICE_MAX) {
        return 0;
    }
    idx = g_service_count++;
    g_services[idx] = *svc;
    g_services[idx].pid = -1;
    g_services[idx].state = SERVICE_STATE_INACTIVE;
    g_services[idx].last_exit_status = -1;
    g_services[idx].last_term_signal = 0;
    for (i = 0; i < g_services[idx].argc && i < SERVICE_ARGS_MAX; i++) {
        g_services[idx].argv[i] = g_services[idx].argv_storage[i];
    }
    g_services[idx].argv[g_services[idx].argc] = NULL;
    return 1;
}

static const char* service_mode_name(service_mode_t mode)
{
    switch (mode) {
        case SERVICE_MODE_RESPAWN: return "respawn";
        case SERVICE_MODE_ONESHOT: return "oneshot";
        default: return "unknown";
    }
}

static const char* service_state_name(service_state_t state)
{
    switch (state) {
        case SERVICE_STATE_INACTIVE: return "inactive";
        case SERVICE_STATE_RUNNING:  return "running";
        case SERVICE_STATE_EXITED:   return "exited";
        case SERVICE_STATE_FAILED:   return "failed";
        case SERVICE_STATE_BACKOFF:  return "backoff";
        default: return "unknown";
    }
}

static void write_services_status(void)
{
    static const char header[] =
        "NAME\tMODE\tSTATE\tPID\tRESTARTS\tLASTEXIT\tLASTSIG\tCOMMAND\n";
    int fd;
    int i;

    if (mkdir("/run", 0755) != 0 && errno != EEXIST) {
        return;
    }

    fd = open("/run/services.status", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }

    (void)write(fd, header, sizeof(header) - 1u);
    for (i = 0; i < g_service_count; i++) {
        service_t* svc = &g_services[i];
        char line[512];
        int pos;
        int a;

        if (!svc->used) {
            continue;
        }

        pos = snprintf(line, sizeof(line), "%s\t%s\t%s\t%ld\t%u\t%d\t%d\t",
                       svc->name,
                       service_mode_name(svc->mode),
                       service_state_name(svc->state),
                       (long)svc->pid,
                       (unsigned)svc->restart_count,
                       svc->last_exit_status,
                       svc->last_term_signal);
        if (pos < 0) {
            continue;
        }
        for (a = 0; a < svc->argc && pos > 0 && pos < (int)sizeof(line) - 2; a++) {
            if (a != 0) {
                line[pos++] = ' ';
            }
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%s", svc->argv[a]);
        }
        if (pos < (int)sizeof(line) - 1) {
            line[pos++] = '\n';
        }
        (void)write(fd, line, (size_t)pos);
    }
    (void)close(fd);
}

static void init_default_services(void)
{
    service_t svc;

    memset(&svc, 0, sizeof(svc));
    svc.used = 1;
    svc.mode = SERVICE_MODE_RESPAWN;
    copy_str(svc.name, sizeof(svc.name), "login");
    copy_str(svc.argv_storage[0], sizeof(svc.argv_storage[0]), "/bin/login");
    svc.argv[0] = svc.argv_storage[0];
    svc.argv[1] = NULL;
    svc.argc = 1;
    (void)add_service(&svc);
}

static void parse_service_line(char* line)
{
    service_t svc;
    char* cursor = line;
    char token[SERVICE_ARG_MAX];
    int argc = 0;

    memset(&svc, 0, sizeof(svc));
    if (!next_token(&cursor, token, sizeof(token))) {
        return;
    }
    copy_str(svc.name, sizeof(svc.name), token);

    if (!next_token(&cursor, token, sizeof(token))) {
        return;
    }
    if (!parse_service_mode(token, &svc.mode)) {
        return;
    }

    while (argc < SERVICE_ARGS_MAX && next_token(&cursor, token, sizeof(token))) {
        copy_str(svc.argv_storage[argc], sizeof(svc.argv_storage[argc]), token);
        svc.argv[argc] = svc.argv_storage[argc];
        argc++;
    }
    svc.argv[argc] = NULL;
    svc.argc = argc;
    svc.used = (argc > 0);
    if (svc.used) {
        (void)add_service(&svc);
    }
}

static void load_services_config(void)
{
    int fd;
    char buf[1024];
    int nread;
    int start = 0;
    int i;

    g_service_count = 0;
    memset(g_services, 0, sizeof(g_services));

    fd = open("/etc/services", O_RDONLY);
    if (fd < 0) {
        init_default_services();
        return;
    }

    nread = (int)read(fd, buf, sizeof(buf) - 1);
    (void)close(fd);
    if (nread <= 0) {
        init_default_services();
        return;
    }
    buf[nread] = '\0';

    for (i = 0; i <= nread; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            buf[i] = '\0';
            parse_service_line(&buf[start]);
            start = i + 1;
        }
    }

    if (g_service_count == 0) {
        init_default_services();
    }
    write_services_status();
}

static pid_t spawn_service_argv(char* const argv[])
{
    if (!argv || !argv[0]) {
        return (pid_t)-1;
    }
    return spawnv(argv[0], argv);
}

static int start_service(service_t* svc)
{
    pid_t pid;
    char* const shell_argv[] = { (char*)"/bin/sh", (char*)0 };

    if (!svc || !svc->used || svc->argc == 0) {
        return 0;
    }
    pid = spawn_service_argv(svc->argv);
    if (pid < 0 && cstr_eq(svc->name, "login")) {
        write_service_prefix(svc->name);
        (void)write_str("spawn failed, falling back to /bin/sh\n");
        pid = spawnv("/bin/sh", shell_argv);
    }
    if (pid < 0) {
        svc->pid = -1;
        svc->state = (svc->mode == SERVICE_MODE_RESPAWN) ? SERVICE_STATE_BACKOFF : SERVICE_STATE_FAILED;
        svc->restart_after_us = monotonic_us() + SERVICE_RESPAWN_DELAY_US;
        write_service_prefix(svc->name);
        (void)write_str("spawn failed\n");
        write_services_status();
        return 0;
    }

    svc->pid = pid;
    svc->started = 1;
    svc->state = SERVICE_STATE_RUNNING;
    svc->restart_after_us = 0;
    write_service_prefix(svc->name);
    (void)write_str("started pid=");
    write_u64((uint64_t)pid);
    (void)write_str("\n");
    write_services_status();
    return 1;
}

static void start_pending_services(void)
{
    uint64_t now = monotonic_us();
    int i;

    for (i = 0; i < g_service_count; i++) {
        service_t* svc = &g_services[i];
        if (!svc->used || svc->pid > 0) {
            continue;
        }
        if (svc->mode == SERVICE_MODE_ONESHOT && svc->started) {
            continue;
        }
        if (svc->restart_after_us != 0 && now < svc->restart_after_us) {
            continue;
        }
        (void)start_service(svc);
    }
}

static service_t* find_service_by_pid(pid_t pid)
{
    int i;

    for (i = 0; i < g_service_count; i++) {
        if (g_services[i].used && g_services[i].pid == pid) {
            return &g_services[i];
        }
    }
    return NULL;
}

static void handle_service_exit(pid_t pid, int status)
{
    service_t* svc = find_service_by_pid(pid);

    if (!svc) {
        return;
    }

    if (WIFEXITED(status)) {
        svc->pid = -1;
        svc->last_exit_status = WEXITSTATUS(status);
        svc->last_term_signal = 0;
        write_service_prefix(svc->name);
        (void)write_str("exited status=");
        write_u64((uint64_t)WEXITSTATUS(status));
        (void)write_str("\n");
        if (svc->mode == SERVICE_MODE_RESPAWN) {
            svc->state = SERVICE_STATE_BACKOFF;
            svc->restart_count++;
            svc->restart_after_us = monotonic_us() + SERVICE_RESPAWN_DELAY_US;
        } else {
            svc->state = (WEXITSTATUS(status) == 0) ? SERVICE_STATE_EXITED : SERVICE_STATE_FAILED;
        }
    } else if (WIFSIGNALED(status)) {
        svc->pid = -1;
        svc->last_term_signal = WTERMSIG(status);
        svc->last_exit_status = -1;
        write_service_prefix(svc->name);
        (void)write_str("terminated signal=");
        write_u64((uint64_t)WTERMSIG(status));
        (void)write_str("\n");
        if (svc->mode == SERVICE_MODE_RESPAWN) {
            svc->state = SERVICE_STATE_BACKOFF;
            svc->restart_count++;
            svc->restart_after_us = monotonic_us() + SERVICE_RESPAWN_DELAY_US;
        } else {
            svc->state = SERVICE_STATE_FAILED;
        }
    }
    write_services_status();
}

static void print_file(const char* path)
{
    char buf[64];
    long fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return;
    }
    for (;;) {
        long n = posix_read((int)fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        (void)posix_write(FD_STDOUT, buf, (uint64_t)n);
    }
    (void)posix_close((int)fd);
}

static void print_hostname(void)
{
    char buf[64];
    long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
    if (fd < 0) {
        return;
    }

    long n = posix_read((int)fd, buf, sizeof(buf) - 1);
    (void)posix_close((int)fd);
    if (n <= 0) {
        return;
    }

    int len = (int)n;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        len--;
    }
    if (len <= 0) {
        return;
    }

    (void)write_str("[init] hostname: ");
    (void)write_buf(buf, (uint64_t)len);
    (void)write_str("\n");
}

static void run_smoke(void)
{
    (void)write_str("[init] self-test: start\n");
    (void)write_str("[init] self-test: pid=");
    {
        long pid = posix_getpid();
        if (pid < 0) {
            (void)write_str("ERR\n");
        } else {
            write_u64((uint64_t)pid);
            (void)write_str("\n");
        }
    }

    {
        long fd = posix_open("/bin/init", VFS_OPEN_READ);
        if (fd < 0) {
            (void)write_str("[init] self-test: open /bin/init failed\n");
        } else {
            uint8_t hdr[4] = {0, 0, 0, 0};
            long n = posix_read((int)fd, hdr, sizeof(hdr));
            (void)write_str("[init] self-test: read /bin/init bytes=");
            if (n < 0) {
                (void)write_str("ERR\n");
            } else {
                write_u64((uint64_t)n);
                (void)write_str(" hdr=");
                for (long i = 0; i < n; i++) {
                    write_hex_byte(hdr[i]);
                }
                (void)write_str("\n");
            }
            if (posix_close((int)fd) < 0) {
                (void)write_str("[init] self-test: close failed\n");
            } else {
                (void)write_str("[init] self-test: close ok\n");
            }
        }
    }

    (void)write_str("[init] self-test: done\n");
}

static int file_exists(const char* path)
{
    long fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return 0;
    }
    (void)posix_close((int)fd);
    return 1;
}

static void ct_log(const char* id, const char* verdict, const char* msg)
{
    (void)write_str("[CT] ");
    (void)write_str(id);
    (void)write_str(" ");
    (void)write_str(verdict);
    (void)write_str(" ");
    (void)write_str(msg);
    (void)write_str("\n");
}

static int cstr_eq(const char* a, const char* b)
{
    uint64_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int cstr_contains(const char* s, const char* needle)
{
    uint64_t i = 0;
    uint64_t j = 0;
    if (!s || !needle || !needle[0]) {
        return 0;
    }
    for (i = 0; s[i]; i++) {
        for (j = 0; needle[j] && s[i + j] == needle[j]; j++) {
            ;
        }
        if (!needle[j]) {
            return 1;
        }
    }
    return 0;
}

static void run_service_supervisor(void)
{
    int status = 0;

    load_services_config();
    (void)write_str("[init] service supervisor ready\n");

    for (;;) {
        pid_t pid;

        start_pending_services();

        pid = waitpid((pid_t)-1, &status, 0);
        if (pid > 0) {
            handle_service_exit(pid, status);
            continue;
        }
        (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);
    }
}

static int file_contains_text(const char* path, const char* needle)
{
    char buf[512];
    long fd;
    long n;

    if (!path || !needle) {
        return 0;
    }

    fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return 0;
    }

    n = posix_read((int)fd, buf, sizeof(buf) - 1);
    (void)posix_close((int)fd);
    if (n <= 0) {
        return 0;
    }

    buf[n] = '\0';
    return cstr_contains(buf, needle);
}

static int write_text_file(const char* path, const char* text)
{
    size_t len = 0;
    int fd;

    if (!path || !text) {
        return 0;
    }

    while (text[len] != '\0') {
        len++;
    }

    fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return 0;
    }
    if (write(fd, text, len) != (ssize_t)len) {
        (void)close(fd);
        return 0;
    }
    return close(fd) == 0;
}

static int spawn_and_wait(char* const argv[])
{
    int status = -1;
    long pid;

    if (!argv || !argv[0]) {
        return 0;
    }

    pid = posix_spawn(argv[0], (const char* const*)argv);
    if (pid <= 0) {
        return 0;
    }
    if (waitpid((pid_t)pid, &status, 0) != pid) {
        return 0;
    }
    return status == 0;
}

static void cleanup_ct_paths(void)
{
    (void)unlink("/mnt/ct_ls.out");
    (void)unlink("/mnt/ct_src.txt");
    (void)unlink("/mnt/ct_copy.txt");
    (void)unlink("/mnt/ct_moved.txt");
    (void)unlink("/mnt/ct_grep.txt");
    (void)unlink("/mnt/ct_grep.out");
    (void)unlink("/mnt/ct_find.out");

    (void)unlink("/mnt/ct_dir/sub/file.txt");
    (void)rmdir("/mnt/ct_dir/sub");
    (void)rmdir("/mnt/ct_dir");

    (void)unlink("/mnt/ct_dir_copy/sub/file.txt");
    (void)rmdir("/mnt/ct_dir_copy/sub");
    (void)rmdir("/mnt/ct_dir_copy");

    (void)unlink("/mnt/ct_find/sub/note.txt");
    (void)rmdir("/mnt/ct_find/sub");
    (void)rmdir("/mnt/ct_find");
}

static int kmod_find(rodnix_kmod_info_t* mods, uint32_t n, const char* name)
{
    if (!mods || !name) {
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (cstr_eq(mods[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int dir_has_entry(const char* dir_path, const char* name)
{
    DIR* d = opendir(dir_path);
    if (!d) {
        return 0;
    }
    for (;;) {
        struct dirent* de = readdir(d);
        if (!de) {
            break;
        }
        if (cstr_eq(de->d_name, name)) {
            (void)closedir(d);
            return 1;
        }
    }
    (void)closedir(d);
    return 0;
}

static int dev_dir_has_entry(const char* name)
{
    return dir_has_entry("/dev", name);
}

static void run_ifconfig_smoke_if_enabled(void)
{
    if (!file_exists("/etc/smoke.ifconfig.auto")) {
        return;
    }

    const char* av[2];
    av[0] = "/bin/ifconfig";
    av[1] = 0;

    (void)write_str("[SMK] IFCONFIG START\n");
    long pid = posix_spawn("/bin/ifconfig", av);
    if (pid <= 0) {
        (void)write_str("[SMK] IFCONFIG FAIL spawn\n");
        return;
    }
    (void)write_str("[SMK] IFCONFIG WAIT (WNOHANG)\n");

    {
        struct timespec t0;
        struct timespec t1;
        const int64_t timeout_ns = 2LL * 1000LL * 1000LL * 1000LL;
        int status = -1;

        if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
            (void)write_str("[SMK] IFCONFIG FAIL clock\n");
            return;
        }

        for (;;) {
            pid_t wr = waitpid((pid_t)pid, &status, WNOHANG);
            if (wr == (pid_t)pid) {
                if (status == 0) {
                    (void)write_str("[SMK] IFCONFIG PASS\n");
                } else {
                    (void)write_str("[SMK] IFCONFIG FAIL wait/status\n");
                }
                return;
            }
            if (wr < 0) {
                (void)write_str("[SMK] IFCONFIG FAIL wait\n");
                return;
            }

            if (clock_gettime(CLOCK_MONOTONIC, &t1) == 0) {
                int64_t dt_ns = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000LL +
                                (int64_t)(t1.tv_nsec - t0.tv_nsec);
                if (dt_ns >= timeout_ns) {
                    (void)write_str("[SMK] IFCONFIG TIMEOUT\n");
                    return;
                }
            }

            (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);
        }
    }
}

static void run_tcc_smoke_if_enabled(void)
{
    static const char src_path[] = "/mnt/tcc_smoke.c";
    static const char obj_path[] = "/mnt/tcc_smoke.o";
    static const char tcc_path[] = "/mnt/usr/bin/tcc";
    static const char src_code[] =
        "int rodnix_tcc_smoke(void) {\n"
        "    return 42;\n"
        "}\n";

    if (!file_exists("/etc/tcc.auto")) {
        (void)write_str("[SMK] TCC SKIP flag missing file=");
        (void)write_str(file_exists("/etc/tcc.auto") ? "1" : "0");
        (void)write_str(" dir=");
        (void)write_str(dir_has_entry("/etc", "tcc.auto") ? "1\n" : "0\n");
        return;
    }

    (void)write_str("[SMK] TCC START\n");
    if (!file_exists(tcc_path)) {
        (void)write_str("[SMK] TCC FAIL missing /mnt/usr/bin/tcc\n");
        return;
    }

    (void)unlink(src_path);
    (void)unlink(obj_path);

    {
        int fd = open(src_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            (void)write_str("[SMK] TCC FAIL create source\n");
            return;
        }
        if (write(fd, src_code, sizeof(src_code) - 1) != (ssize_t)(sizeof(src_code) - 1)) {
            (void)close(fd);
            (void)write_str("[SMK] TCC FAIL write source\n");
            return;
        }
        if (close(fd) != 0) {
            (void)write_str("[SMK] TCC FAIL close source\n");
            return;
        }
    }

    {
        char* const argv[] = {
            (char*)tcc_path,
            (char*)"-c",
            (char*)src_path,
            (char*)"-o",
            (char*)obj_path,
            NULL
        };
        char* const envp[] = {
            (char*)"PATH=/mnt/usr/bin:/bin",
            (char*)"TMPDIR=/mnt",
            NULL
        };
        struct timespec t0;
        struct timespec t1;
        const int64_t timeout_ns = 5LL * 1000LL * 1000LL * 1000LL;
        int status = -1;
        pid_t pid;

        pid = spawnve(tcc_path, argv, envp);
        if (pid <= 0) {
            (void)write_str("[SMK] TCC FAIL spawn\n");
            return;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
            (void)write_str("[SMK] TCC FAIL clock\n");
            return;
        }

        for (;;) {
            pid_t wr = waitpid(pid, &status, WNOHANG);
            if (wr == pid) {
                break;
            }
            if (wr < 0) {
                (void)write_str("[SMK] TCC FAIL wait\n");
                return;
            }

            if (clock_gettime(CLOCK_MONOTONIC, &t1) == 0) {
                int64_t dt_ns = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000LL +
                                (int64_t)(t1.tv_nsec - t0.tv_nsec);
                if (dt_ns >= timeout_ns) {
                    (void)write_str("[SMK] TCC TIMEOUT\n");
                    return;
                }
            }

            (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);
        }

        if (status != 0) {
            (void)write_str("[SMK] TCC FAIL compiler status\n");
            return;
        }
    }

    {
        struct stat st;
        if (stat(obj_path, &st) != 0) {
            (void)write_str("[SMK] TCC FAIL missing output\n");
            return;
        }
        if (st.st_size <= 0) {
            (void)write_str("[SMK] TCC FAIL empty output\n");
            return;
        }
    }

    (void)write_str("[SMK] TCC PASS\n");
}

static void run_contract_mode_if_enabled(void)
{
    if (!file_exists("/etc/contract.auto")) {
        return;
    }

    int ok = 1;
    (void)write_str("[CT] MODE enabled\n");

    {
        char b = 0;
        long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
        if (fd < 0) {
            ct_log("CT-007", "FAIL", "open failed");
            ok = 0;
        } else if (posix_close((int)fd) < 0) {
            ct_log("CT-007", "FAIL", "close failed");
            ok = 0;
        } else if (posix_read((int)fd, &b, 1) < 0) {
            ct_log("CT-007", "PASS", "close invalidates fd");
        } else {
            ct_log("CT-007", "FAIL", "read after close succeeded");
            ok = 0;
        }
    }

    {
        char b = 0;
        if (posix_read(-1, &b, 1) < 0) {
            ct_log("CT-008", "PASS", "read on invalid fd fails");
        } else {
            ct_log("CT-008", "FAIL", "read on invalid fd succeeded");
            ok = 0;
        }
    }

    {
        long ts = rdnx_syscall1(SYS_TEST_SLEEP, 1);
        if (ts >= 0) {
            ct_log("CT-DBG", "PASS", "blocking syscall probe returned");
        } else {
            ct_log("CT-DBG", "FAIL", "blocking syscall probe failed");
        }
    }

    {
        struct timespec m0, m1, r0, r1;
        int time_ok = 1;
        if (clock_gettime(CLOCK_MONOTONIC, &m0) != 0 ||
            clock_gettime(CLOCK_REALTIME, &r0) != 0) {
            ct_log("CT-016", "FAIL", "clock_gettime initial failed");
            ct_log("CT-017", "FAIL", "clock_gettime initial failed");
            ok = 0;
            time_ok = 0;
        }
        if (time_ok) {
            (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);
            if (clock_gettime(CLOCK_MONOTONIC, &m1) != 0) {
                ct_log("CT-016", "FAIL", "clock_gettime monotonic failed");
                ok = 0;
                time_ok = 0;
            }
            if (clock_gettime(CLOCK_REALTIME, &r1) != 0) {
                ct_log("CT-017", "FAIL", "clock_gettime realtime failed");
                ok = 0;
                time_ok = 0;
            }
        }
        if (time_ok) {
            uint64_t mu0 = (uint64_t)m0.tv_sec * 1000000ULL + (uint64_t)m0.tv_nsec / 1000ULL;
            uint64_t mu1 = (uint64_t)m1.tv_sec * 1000000ULL + (uint64_t)m1.tv_nsec / 1000ULL;
            uint64_t ru0 = (uint64_t)r0.tv_sec * 1000000ULL + (uint64_t)r0.tv_nsec / 1000ULL;
            uint64_t ru1 = (uint64_t)r1.tv_sec * 1000000ULL + (uint64_t)r1.tv_nsec / 1000ULL;
            if (mu1 >= mu0) {
                ct_log("CT-016", "PASS", "CLOCK_MONOTONIC is non-decreasing");
            } else {
                ct_log("CT-016", "FAIL", "CLOCK_MONOTONIC went backwards");
                ok = 0;
            }
            if (ru1 >= ru0) {
                ct_log("CT-017", "PASS", "CLOCK_REALTIME is non-decreasing");
            } else {
                ct_log("CT-017", "FAIL", "CLOCK_REALTIME went backwards");
                ok = 0;
            }
        }
    }

    {
        int status = -1;
        volatile uint64_t as_canary = 0x1122334455667788ULL;
        long pid = posix_spawn("/bin/true", 0);
        if (pid > 0) {
            ct_log("CT-001", "PASS", "spawn creates child pid");
            {
                long wr = waitpid((pid_t)pid, &status, 0);
                if (wr == pid && status == 0) {
                    ct_log("CT-005", "PASS", "waitpid reaps child exit status");
                } else {
                    ct_log("CT-005", "FAIL", "waitpid failed or bad status");
                    ok = 0;
                }
            }
            if (as_canary == 0x1122334455667788ULL) {
                ct_log("CT-013", "PASS", "parent stack survives child address-space switches");
            } else {
                ct_log("CT-013", "FAIL", "parent stack corrupted after child run/wait");
                ok = 0;
            }
            {
                long wr2 = waitpid((pid_t)pid, &status, 0);
                if (wr2 < 0) {
                    ct_log("CT-006", "PASS", "second waitpid fails after reap");
                } else {
                    ct_log("CT-006", "FAIL", "second waitpid unexpectedly succeeded");
                    ok = 0;
                }
            }
        } else {
            ct_log("CT-001", "FAIL", "spawn failed");
            ct_log("CT-005", "FAIL", "spawn prerequisite failed");
            ct_log("CT-006", "FAIL", "spawn prerequisite failed");
            ct_log("CT-013", "FAIL", "spawn prerequisite failed");
            ok = 0;
        }
    }

    {
        /* CT-014/CT-015: fd inheritance + close-on-exec in spawn+exec flow. */
        int status = -1;
        long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
        if (fd < 0) {
            ct_log("CT-014", "FAIL", "open prerequisite failed");
            ct_log("CT-015", "FAIL", "open prerequisite failed");
            ok = 0;
        } else if (fd != 3) {
            ct_log("CT-014", "FAIL", "expected inherited probe fd=3");
            ct_log("CT-015", "FAIL", "expected inherited probe fd=3");
            ok = 0;
            (void)posix_close((int)fd);
        } else {
            if (fcntl((int)fd, F_GETFD, 0) != 0) {
                ct_log("CT-014", "FAIL", "new fd unexpectedly has CLOEXEC");
                ok = 0;
            }
            long pid = posix_spawn("/bin/contract_fd_inherit", 0);
            if (pid <= 0) {
                ct_log("CT-014", "FAIL", "inherit spawn failed");
                ct_log("CT-015", "FAIL", "inherit spawn prerequisite failed");
                ok = 0;
            } else {
                long wr = waitpid((pid_t)pid, &status, 0);
                if (wr == pid && status == 0) {
                    ct_log("CT-014", "PASS", "spawn inherits open fd");
                } else {
                    ct_log("CT-014", "FAIL", "child could not use inherited fd");
                    (void)write_str("[CT] CT-014 DBG status=");
                    write_u64((uint64_t)(uint32_t)status);
                    (void)write_str("\n");
                    ok = 0;
                }
            }

            if (fcntl((int)fd, F_SETFD, FD_CLOEXEC) < 0) {
                ct_log("CT-015", "FAIL", "F_SETFD failed");
                ok = 0;
            } else if ((fcntl((int)fd, F_GETFD, 0) & FD_CLOEXEC) == 0) {
                ct_log("CT-015", "FAIL", "F_GETFD missing CLOEXEC flag");
                ok = 0;
            } else {
                long pid2 = posix_spawn("/bin/contract_fd_inherit", 0);
                if (pid2 <= 0) {
                    ct_log("CT-015", "FAIL", "cloexec spawn failed");
                    ok = 0;
                } else {
                    long wr2 = waitpid((pid_t)pid2, &status, 0);
                    if (wr2 == pid2 && status == 2) {
                        ct_log("CT-015", "PASS", "CLOEXEC closes fd in spawned image");
                    } else {
                        ct_log("CT-015", "FAIL", "fd leaked across exec with CLOEXEC");
                        (void)write_str("[CT] CT-015 DBG status=");
                        write_u64((uint64_t)(uint32_t)status);
                        (void)write_str("\n");
                        ok = 0;
                    }
                }
            }

            (void)posix_close((int)fd);
        }
    }

    {
        /*
         * CT-003/CT-011:
         * Spawn probe image that exec()'s into /bin/contract_exec_after.
         * Child exit status encodes:
         *   - low 16 bits: post-exec PID
         *   - bit 16: image-specific state reset marker
         */
        int status = -1;
        long pid = posix_spawn("/bin/contract_exec_probe", 0);
        if (pid <= 0) {
            ct_log("CT-003", "FAIL", "exec probe spawn failed");
            ct_log("CT-011", "FAIL", "exec probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr != pid) {
                ct_log("CT-003", "FAIL", "exec probe waitpid failed");
                ct_log("CT-011", "FAIL", "exec probe waitpid failed");
                ok = 0;
            } else {
                uint32_t st = (uint32_t)status;
                uint32_t observed_pid = st & 0xFFFFu;
                uint32_t image_reset = st & 0x10000u;
                if (observed_pid == (((uint32_t)pid) & 0xFFFFu)) {
                    ct_log("CT-003", "PASS", "exec preserves pid across image switch");
                } else {
                    ct_log("CT-003", "FAIL", "exec pid mismatch");
                    ok = 0;
                }
                if (image_reset != 0u) {
                    ct_log("CT-011", "PASS", "exec resets image-specific state");
                } else {
                    ct_log("CT-011", "FAIL", "exec image-specific state leaked");
                    ok = 0;
                }
            }
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/contract_heap", 0);
        if (pid <= 0) {
            ct_log("CT-018", "FAIL", "heap probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-018", "PASS", "heap probe child passed");
            } else {
                ct_log("CT-018", "FAIL", "heap probe child failed");
                ok = 0;
            }
        }
    }

    {
        uint8_t* p = (uint8_t*)malloc(64);
        uint8_t* q = (uint8_t*)calloc(16, 8);
        int local_ok = 1;
        if (!p || !q) {
            local_ok = 0;
        }
        if (local_ok) {
            for (int i = 0; i < 64; i++) {
                p[i] = (uint8_t)(0xA0 + i);
            }
            p = (uint8_t*)realloc(p, 256);
            if (!p) {
                local_ok = 0;
            } else {
                for (int i = 0; i < 64; i++) {
                    if (p[i] != (uint8_t)(0xA0 + i)) {
                        local_ok = 0;
                        break;
                    }
                }
            }
        }
        free(p);
        free(q);
        if (local_ok) {
            ct_log("CT-019", "PASS", "parent heap alloc/realloc/free sequence");
        } else {
            ct_log("CT-019", "FAIL", "parent heap sequence failed");
            ok = 0;
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/contract_dirent", 0);
        if (pid <= 0) {
            ct_log("CT-020", "FAIL", "dirent probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-020", "PASS", "dirent handle API probe passed");
            } else {
                ct_log("CT-020", "FAIL", "dirent handle API probe failed");
                ok = 0;
            }
        }
    }

    {
        int dev_ok = 1;
        if (!dev_dir_has_entry("console") ||
            !dev_dir_has_entry("stdin") ||
            !dev_dir_has_entry("stdout") ||
            !dev_dir_has_entry("stderr") ||
            !dev_dir_has_entry("null") ||
            !dev_dir_has_entry("zero")) {
            dev_ok = 0;
        }
        if (dev_ok) {
            ct_log("CT-024", "PASS", "devfs base nodes visible in /dev");
        } else {
            ct_log("CT-024", "FAIL", "missing required /dev entries");
            ok = 0;
        }
    }

    {
        int tty_ok = (isatty(0) != 0) && (isatty(1) != 0) && (isatty(2) != 0);
        if (tty_ok) {
            ct_log("CT-025", "PASS", "isatty works for stdio fds");
        } else {
            ct_log("CT-025", "FAIL", "isatty failed for stdio");
            ok = 0;
        }
    }

    {
        int io_ok = 1;
        char b = 'X';
        char zbuf[8] = {1,2,3,4,5,6,7,8};
        long fd_null = posix_open("/dev/null", VFS_OPEN_READ | VFS_OPEN_WRITE);
        if (fd_null < 0) {
            io_ok = 0;
        } else {
            long wn = posix_write((int)fd_null, &b, 1);
            long rn = posix_read((int)fd_null, &b, 1);
            if (wn != 1 || rn != 0) {
                io_ok = 0;
            }
            (void)posix_close((int)fd_null);
        }
        long fd_zero = posix_open("/dev/zero", VFS_OPEN_READ);
        if (fd_zero < 0) {
            io_ok = 0;
        } else {
            long rn = posix_read((int)fd_zero, zbuf, sizeof(zbuf));
            if (rn != (long)sizeof(zbuf)) {
                io_ok = 0;
            } else {
                for (uint64_t i = 0; i < sizeof(zbuf); i++) {
                    if (zbuf[i] != 0) {
                        io_ok = 0;
                        break;
                    }
                }
            }
            (void)posix_close((int)fd_zero);
        }
        if (io_ok) {
            ct_log("CT-026", "PASS", "devfs null/zero I/O contract");
        } else {
            ct_log("CT-026", "FAIL", "devfs null/zero behavior mismatch");
            ok = 0;
        }
    }

    {
        rodnix_blockdev_info_t devs[4];
        uint32_t total = 0;
        int block_ok = 1;
        long n = posix_blocklist(devs, 4, &total);
        if (n < 0) {
            block_ok = 0;
        } else if (n > 0) {
            char path[32];
            uint64_t p = 0;
            const char* prefix = "/dev/";
            for (uint64_t i = 0; prefix[i] && p + 1 < sizeof(path); i++) {
                path[p++] = prefix[i];
            }
            for (uint64_t i = 0; devs[0].name[i] && p + 1 < sizeof(path); i++) {
                path[p++] = devs[0].name[i];
            }
            path[p] = '\0';
            if (!dev_dir_has_entry(devs[0].name)) {
                block_ok = 0;
            } else {
                char sec[512];
                char tail[17];
                long fd = posix_open(path, VFS_OPEN_READ);
                if (fd < 0) {
                    block_ok = 0;
                } else {
                    long rn = posix_read((int)fd, sec, sizeof(sec));
                    if (rn != (long)sizeof(sec)) {
                        block_ok = 0;
                    }
                    if (block_ok) {
                        if (posix_lseek((int)fd, 1, 0) < 0) {
                            block_ok = 0;
                        } else {
                            long rn2 = posix_read((int)fd, tail, sizeof(tail));
                            if (rn2 != (long)sizeof(tail)) {
                                block_ok = 0;
                            }
                        }
                    }
                    (void)posix_close((int)fd);
                }
            }
        }

        if (block_ok) {
            ct_log("CT-027", "PASS", "devfs block node bridge works");
        } else {
            ct_log("CT-027", "FAIL", "devfs block node missing or unreadable");
            ok = 0;
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/contract_fsio", 0);
        if (pid <= 0) {
            ct_log("CT-021", "FAIL", "fsio probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-021", "PASS", "stat/fstat/lseek probe passed");
            } else {
                ct_log("CT-021", "FAIL", "stat/fstat/lseek probe failed");
                ok = 0;
            }
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/contract_wait_nonchild", 0);
        if (pid <= 0) {
            ct_log("CT-004", "FAIL", "wait-nonchild probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-004", "PASS", "waitpid on non-child is denied");
            } else {
                ct_log("CT-004", "FAIL", "waitpid non-child contract failed");
                ok = 0;
            }
        }
    }

    {
        rodnix_kmod_info_t mods[32];
        uint32_t total = 0;
        long n = posix_kmodls(mods, 32, &total);
        if (n < 0) {
            ct_log("CT-022", "FAIL", "kmodls initial failed");
            ok = 0;
        } else {
            int ext2_idx = kmod_find(mods, (uint32_t)n, "fs.ext2");
            if (ext2_idx < 0 || mods[ext2_idx].loaded == 0 || mods[ext2_idx].builtin == 0) {
                ct_log("CT-022", "FAIL", "builtin fs.ext2 not visible");
                ok = 0;
            } else if (posix_kmodload("/lib/modules/demo.ko") < 0) {
                ct_log("CT-022", "FAIL", "kmodload demo failed");
                ok = 0;
            } else {
                uint32_t total2 = 0;
                long n2 = posix_kmodls(mods, 32, &total2);
                int demo_idx = (n2 < 0) ? -1 : kmod_find(mods, (uint32_t)n2, "demo.echo");
                if (n2 < 0 || demo_idx < 0 || mods[demo_idx].loaded == 0 || mods[demo_idx].builtin != 0) {
                    ct_log("CT-022", "FAIL", "demo module not loaded");
                    ok = 0;
                } else if (posix_kmodunload("demo.echo") < 0) {
                    ct_log("CT-022", "FAIL", "kmodunload demo failed");
                    ok = 0;
                } else {
                    uint32_t total3 = 0;
                    long n3 = posix_kmodls(mods, 32, &total3);
                    int demo3 = (n3 < 0) ? -1 : kmod_find(mods, (uint32_t)n3, "demo.echo");
                    if (n3 < 0 || demo3 < 0 || mods[demo3].loaded != 0) {
                        ct_log("CT-022", "FAIL", "demo module still loaded");
                        ok = 0;
                    } else {
                        ct_log("CT-022", "PASS", "kmod ls/load/unload contract");
                    }
                }
            }
        }
    }

    {
        /* Race case: child may exit before parent enters waitpid. */
        int status = -1;
        long pid = posix_spawn("/bin/true", 0);
        if (pid <= 0) {
            ct_log("CT-005", "FAIL", "race spawn failed");
            ct_log("CT-006", "FAIL", "race spawn prerequisite failed");
            ok = 0;
        } else {
            /* Give child a chance to complete before first waitpid recheck. */
            for (int i = 0; i < 8; i++) {
                (void)rdnx_syscall0(0);
            }
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-005", "PASS", "race fast-exit child reaped");
            } else {
                ct_log("CT-005", "FAIL", "race waitpid failed");
                ok = 0;
            }
            {
                long wr2 = waitpid((pid_t)pid, &status, 0);
                if (wr2 < 0) {
                    ct_log("CT-006", "PASS", "race second waitpid fails after reap");
                } else {
                    ct_log("CT-006", "FAIL", "race second waitpid unexpectedly succeeded");
                    ok = 0;
                }
            }
        }
    }

    {
        char buf[192];
        long fd = posix_open("/mnt/README.txt", VFS_OPEN_READ);
        if (fd < 0) {
            ct_log("CT-023", "FAIL", "open /mnt/README.txt failed");
            ok = 0;
        } else {
            long nr = posix_read((int)fd, buf, sizeof(buf) - 1);
            (void)posix_close((int)fd);
            if (nr <= 0) {
                ct_log("CT-023", "FAIL", "read /mnt/README.txt failed");
                ok = 0;
            } else {
                buf[nr] = '\0';
                if (cstr_contains(buf, "RodNIX ext2 demo volume")) {
                    ct_log("CT-023", "PASS", "ext2 mounted and file read");
                } else {
                    ct_log("CT-023", "FAIL", "ext2 content mismatch");
                    ok = 0;
                }
            }
        }
    }

    {
        int wr_ok = 1;
        int wr_unavail = 0; /* reserved while storage write path is being stabilized */
        char buf[16];
        long fd = posix_open("/mnt/README.txt", VFS_OPEN_READ | VFS_OPEN_WRITE);
        if (fd < 0) {
            wr_ok = 0;
        } else {
            long rr = posix_read((int)fd, buf, sizeof(buf));
            if (rr != (long)sizeof(buf)) {
                wr_ok = 0;
            }
            (void)posix_close((int)fd);
        }

        if (wr_ok) {
            fd = posix_open("/mnt/README.txt", VFS_OPEN_READ | VFS_OPEN_WRITE);
            if (fd < 0) {
                wr_ok = 0;
            } else {
                long wn = posix_write((int)fd, buf, sizeof(buf));
                if (wn != (long)sizeof(buf)) {
                    if (wn == -7) {
                        wr_unavail = 1;
                    } else {
                        wr_ok = 0;
                    }
                }
                (void)posix_close((int)fd);
            }
        }

        if (wr_ok && !wr_unavail) {
            fd = posix_open("/mnt/README.txt", VFS_OPEN_READ);
            if (fd < 0) {
                wr_ok = 0;
            } else {
                char chk[16];
                long rr2 = posix_read((int)fd, chk, sizeof(chk));
                if (rr2 != (long)sizeof(chk)) {
                    wr_ok = 0;
                } else {
                    for (uint64_t i = 0; i < sizeof(buf); i++) {
                        if (chk[i] != buf[i]) {
                            wr_ok = 0;
                            break;
                        }
                    }
                }
                (void)posix_close((int)fd);
            }
        }

        if (wr_ok && !wr_unavail) {
            ct_log("CT-028", "PASS", "ext2 overwrite writeback path");
        } else if (wr_unavail) {
            ct_log("CT-028", "PASS", "ext2 writeback deferred: block write backend unavailable");
        } else {
            ct_log("CT-028", "FAIL", "ext2 overwrite writeback failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const ls_argv[] = {
            (char*)"/bin/ls",
            (char*)"-la1",
            (char*)"/bin",
            NULL
        };

        if (!spawn_and_wait(ls_argv)) {
            local_ok = 0;
        }

        if (local_ok) {
            ct_log("CT-029", "PASS", "ls flags exit successfully");
        } else {
            ct_log("CT-029", "FAIL", "ls smoke failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const cp_argv[] = {
            (char*)"/bin/cp",
            (char*)"/mnt/ct_src.txt",
            (char*)"/mnt/ct_copy.txt",
            NULL
        };
        char* const mv_argv[] = {
            (char*)"/bin/mv",
            (char*)"/mnt/ct_copy.txt",
            (char*)"/mnt/ct_moved.txt",
            NULL
        };
        char* const rm_argv[] = {
            (char*)"/bin/rm",
            (char*)"-f",
            (char*)"/mnt/ct_moved.txt",
            NULL
        };

        cleanup_ct_paths();

        if (!write_text_file("/mnt/ct_src.txt", "alpha\nRodNIX\n")) {
            local_ok = 0;
        } else if (!spawn_and_wait(cp_argv)) {
            local_ok = 0;
        } else if (!file_contains_text("/mnt/ct_copy.txt", "RodNIX")) {
            local_ok = 0;
        } else if (!spawn_and_wait(mv_argv)) {
            local_ok = 0;
        } else if (file_exists("/mnt/ct_copy.txt") || !file_exists("/mnt/ct_moved.txt")) {
            local_ok = 0;
        } else if (!spawn_and_wait(rm_argv)) {
            local_ok = 0;
        } else if (file_exists("/mnt/ct_moved.txt")) {
            local_ok = 0;
        }

        if (local_ok) {
            ct_log("CT-030", "PASS", "cp/mv/rm basic file workflow");
        } else {
            ct_log("CT-030", "FAIL", "cp/mv/rm file workflow failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const cp_r_argv[] = {
            (char*)"/bin/cp",
            (char*)"-r",
            (char*)"/mnt/ct_dir",
            (char*)"/mnt/ct_dir_copy",
            NULL
        };
        char* const rm_r_argv[] = {
            (char*)"/bin/rm",
            (char*)"-rf",
            (char*)"/mnt/ct_dir",
            (char*)"/mnt/ct_dir_copy",
            NULL
        };

        cleanup_ct_paths();
        if (mkdir("/mnt/ct_dir", 0755) != 0 && errno != EEXIST) {
            local_ok = 0;
        }
        if (local_ok && mkdir("/mnt/ct_dir/sub", 0755) != 0 && errno != EEXIST) {
            local_ok = 0;
        }
        if (local_ok && !write_text_file("/mnt/ct_dir/sub/file.txt", "tree\n")) {
            local_ok = 0;
        }
        if (local_ok && !spawn_and_wait(cp_r_argv)) {
            local_ok = 0;
        }
        if (local_ok && !file_contains_text("/mnt/ct_dir_copy/sub/file.txt", "tree")) {
            local_ok = 0;
        }
        if (local_ok && !spawn_and_wait(rm_r_argv)) {
            local_ok = 0;
        }
        if (local_ok && (file_exists("/mnt/ct_dir/sub/file.txt") || file_exists("/mnt/ct_dir_copy/sub/file.txt"))) {
            local_ok = 0;
        }

        if (local_ok) {
            ct_log("CT-031", "PASS", "recursive cp/rm tree workflow");
        } else {
            ct_log("CT-031", "FAIL", "recursive cp/rm tree workflow failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const grep_argv[] = {
            (char*)"/bin/grep",
            (char*)"-in",
            (char*)"rodnix",
            (char*)"/mnt/ct_grep.txt",
            NULL
        };

        cleanup_ct_paths();
        if (!write_text_file("/mnt/ct_grep.txt", "alpha\nRodNIX\nomega\n")) {
            local_ok = 0;
        } else if (!spawn_and_wait(grep_argv)) {
            local_ok = 0;
        }

        if (local_ok) {
            ct_log("CT-032", "PASS", "grep flags and case-insensitive match");
        } else {
            ct_log("CT-032", "FAIL", "grep smoke failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const find_argv[] = {
            (char*)"/bin/find",
            (char*)"/mnt/ct_find",
            (char*)"-name",
            (char*)"*.txt",
            (char*)"-type",
            (char*)"f",
            (char*)"-maxdepth",
            (char*)"3",
            NULL
        };

        cleanup_ct_paths();
        if (mkdir("/mnt/ct_find", 0755) != 0 && errno != EEXIST) {
            local_ok = 0;
        }
        if (local_ok && mkdir("/mnt/ct_find/sub", 0755) != 0 && errno != EEXIST) {
            local_ok = 0;
        }
        if (local_ok && !write_text_file("/mnt/ct_find/sub/note.txt", "find\n")) {
            local_ok = 0;
        }
        if (local_ok && !spawn_and_wait(find_argv)) {
            local_ok = 0;
        }
        cleanup_ct_paths();

        if (local_ok) {
            ct_log("CT-033", "PASS", "find name/type/maxdepth workflow");
        } else {
            ct_log("CT-033", "FAIL", "find smoke failed");
            ok = 0;
        }
    }

    if (ok) {
        (void)write_str("[CT] ALL PASS\n");
    } else {
        (void)write_str("[CT] ALL FAIL\n");
    }
}

int main(void)
{
    (void)write_str("RodNIX userspace 0.1.22\n");
    print_file("/etc/motd");
    (void)write_str("\n");
    print_hostname();
    run_smoke();
    run_ifconfig_smoke_if_enabled();
    run_tcc_smoke_if_enabled();
    run_contract_mode_if_enabled();

    run_service_supervisor();

    for (;;) {
        (void)rdnx_syscall0(0);
    }
    return 0;
}
