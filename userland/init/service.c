#include "service.h"
#include "common.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include "unistd.h"
#include "sys/wait.h"

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
    int enabled;
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

static uint64_t monotonic_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

static void supervisor_pause_briefly(void)
{
    uint64_t start = monotonic_us();
    uint64_t now;

    if (start == 0) {
        return;
    }

    do {
        now = monotonic_us();
    } while (now != 0 && (now - start) < 10000ULL);
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
    svc.enabled = 1;
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
    svc.enabled = 1;
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
    uint64_t now;
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
        now = monotonic_us();
        svc->pid = -1;
        svc->state = (svc->mode == SERVICE_MODE_RESPAWN) ? SERVICE_STATE_BACKOFF : SERVICE_STATE_FAILED;
        svc->restart_after_us = now ? (now + SERVICE_RESPAWN_DELAY_US) : 0;
        write_service_prefix(svc->name);
        (void)write_str("spawn failed\n");
        write_services_status();
        return 0;
    }

    svc->pid = pid;
    svc->started = 1;
    svc->enabled = 1;
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
        if (!svc->enabled) {
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
    uint64_t now = monotonic_us();

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
        if (!svc->enabled) {
            svc->state = SERVICE_STATE_INACTIVE;
            svc->restart_after_us = 0;
        } else if (svc->mode == SERVICE_MODE_RESPAWN) {
            svc->state = SERVICE_STATE_BACKOFF;
            svc->restart_count++;
            svc->restart_after_us = now ? (now + SERVICE_RESPAWN_DELAY_US) : 0;
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
        if (!svc->enabled) {
            svc->state = SERVICE_STATE_INACTIVE;
            svc->restart_after_us = 0;
        } else if (svc->mode == SERVICE_MODE_RESPAWN) {
            svc->state = SERVICE_STATE_BACKOFF;
            svc->restart_count++;
            svc->restart_after_us = now ? (now + SERVICE_RESPAWN_DELAY_US) : 0;
        } else {
            svc->state = SERVICE_STATE_FAILED;
        }
    }
    write_services_status();
}

static service_t* find_service_by_name(const char* name)
{
    int i;

    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < g_service_count; i++) {
        if (g_services[i].used && cstr_eq(g_services[i].name, name)) {
            return &g_services[i];
        }
    }
    return NULL;
}

static void service_stop(service_t* svc, int keep_enabled)
{
    if (!svc) {
        return;
    }
    svc->enabled = keep_enabled ? 1 : 0;
    svc->restart_after_us = 0;
    if (svc->pid > 0) {
        (void)kill(svc->pid, SIGTERM);
        write_services_status();
    } else {
        svc->state = SERVICE_STATE_INACTIVE;
        write_services_status();
    }
}

static void service_start(service_t* svc)
{
    if (!svc) {
        return;
    }
    svc->enabled = 1;
    svc->restart_after_us = 0;
    if (svc->pid <= 0) {
        if (svc->mode == SERVICE_MODE_ONESHOT) {
            svc->started = 0;
        }
        (void)start_service(svc);
    } else {
        write_services_status();
    }
}

static void handle_control_command_line(char* line)
{
    char* cursor = line;
    char cmd[SERVICE_ARG_MAX];
    char name[SERVICE_ARG_MAX];
    service_t* svc;

    if (!next_token(&cursor, cmd, sizeof(cmd))) {
        return;
    }

    if (cstr_eq(cmd, "status")) {
        write_services_status();
        return;
    }

    if (!next_token(&cursor, name, sizeof(name))) {
        return;
    }

    svc = find_service_by_name(name);
    if (!svc) {
        return;
    }

    if (cstr_eq(cmd, "start")) {
        service_start(svc);
    } else if (cstr_eq(cmd, "stop")) {
        service_stop(svc, 0);
    } else if (cstr_eq(cmd, "restart")) {
        service_stop(svc, 1);
        if (svc->pid <= 0) {
            service_start(svc);
        }
    }
}

static void process_control_commands(void)
{
    int fd;
    char buf[256];
    int nread;
    int start = 0;
    int i;

    fd = open("/run/services.control", O_RDONLY);
    if (fd < 0) {
        return;
    }

    nread = (int)read(fd, buf, sizeof(buf) - 1);
    (void)close(fd);
    (void)unlink("/run/services.control");
    if (nread <= 0) {
        return;
    }
    buf[nread] = '\0';

    for (i = 0; i <= nread; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            buf[i] = '\0';
            handle_control_command_line(&buf[start]);
            start = i + 1;
        }
    }
}

static void reap_service_events(void)
{
    int i;
    int status = 0;

    for (i = 0; i < g_service_count; i++) {
        service_t* svc = &g_services[i];
        pid_t pid;

        if (!svc->used || svc->pid <= 0) {
            continue;
        }

        pid = waitpid(svc->pid, &status, WNOHANG);
        if (pid > 0) {
            handle_service_exit(pid, status);
        }
    }
}

void run_service_supervisor(void)
{
    load_services_config();
    (void)write_str("[init] service supervisor ready\n");

    for (;;) {
        process_control_commands();
        reap_service_events();
        process_control_commands();
        start_pending_services();
        supervisor_pause_briefly();
    }
}
