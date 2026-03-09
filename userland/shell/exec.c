#include "shell_internal.h"

typedef struct sh_exec_spec {
    char* argv[SH_ARG_MAX + 1];
    int argc;
    char in_path[SH_PATH_MAX];
    char out_path[SH_PATH_MAX];
} sh_exec_spec_t;

static int tokenize_meta(char* line, char** toks, int max_toks)
{
    int n = 0;
    int i = 0;
    while (line[i] != '\0' && n < max_toks) {
        while (line[i] == ' ' || line[i] == '\t') {
            i++;
        }
        if (line[i] == '\0') {
            break;
        }
        if (line[i] == '|' || line[i] == '<' || line[i] == '>') {
            toks[n++] = &line[i];
            i++;
            if (line[i] != '\0') {
                line[i] = '\0';
            }
            continue;
        }
        toks[n++] = &line[i];
        while (line[i] != '\0' &&
               line[i] != ' ' && line[i] != '\t' &&
               line[i] != '|' && line[i] != '<' && line[i] != '>') {
            i++;
        }
        if (line[i] != '\0') {
            line[i++] = '\0';
        }
    }
    return n;
}

static void spec_init(sh_exec_spec_t* s)
{
    if (!s) {
        return;
    }
    s->argc = 0;
    s->in_path[0] = '\0';
    s->out_path[0] = '\0';
    for (int i = 0; i < SH_ARG_MAX + 1; i++) {
        s->argv[i] = 0;
    }
}

static int shell_restore_stdio_console(void)
{
    (void)posix_close(FD_STDIN);
    (void)posix_close(FD_STDOUT);
    (void)posix_close(FD_STDERR);
    if (posix_open("/dev/console", VFS_OPEN_READ | VFS_OPEN_WRITE) != FD_STDIN) {
        return -1;
    }
    if (posix_open("/dev/console", VFS_OPEN_WRITE) != FD_STDOUT) {
        return -1;
    }
    if (posix_open("/dev/console", VFS_OPEN_WRITE) != FD_STDERR) {
        return -1;
    }
    return 0;
}

static int shell_apply_redir(const char* in_path, const char* out_path)
{
    char resolved[SH_PATH_MAX];
    if (in_path && in_path[0] != '\0') {
        resolve_path(in_path, resolved, (int)sizeof(resolved));
        (void)posix_close(FD_STDIN);
        if (posix_open(resolved, VFS_OPEN_READ) != FD_STDIN) {
            return -1;
        }
    }
    if (out_path && out_path[0] != '\0') {
        resolve_path(out_path, resolved, (int)sizeof(resolved));
        (void)posix_close(FD_STDOUT);
        if (posix_open(resolved, VFS_OPEN_WRITE | VFS_OPEN_CREATE | VFS_OPEN_TRUNC) != FD_STDOUT) {
            return -1;
        }
    }
    return 0;
}

static int cmd_run_with_redir(sh_exec_spec_t* spec, int verbose)
{
    int rc = -1;
    if (!spec || spec->argc <= 0 || !spec->argv[0]) {
        return -1;
    }
    if (shell_apply_redir(spec->in_path, spec->out_path) != 0) {
        (void)shell_restore_stdio_console();
        return -1;
    }
    rc = cmd_autorun(spec->argc, spec->argv);
    if (verbose && rc < 0) {
        (void)write_str("run: failed\n");
    }
    (void)shell_restore_stdio_console();
    return rc;
}

int cmd_exec_meta_line(char* line)
{
    char* toks[SH_ARG_MAX * 2];
    int nt = tokenize_meta(line, toks, (int)(sizeof(toks) / sizeof(toks[0])));
    if (nt <= 0) {
        return -1;
    }

    sh_exec_spec_t left;
    sh_exec_spec_t right;
    spec_init(&left);
    spec_init(&right);
    sh_exec_spec_t* cur = &left;
    int have_pipe = 0;

    for (int i = 0; i < nt; i++) {
        char* t = toks[i];
        if (!t || t[0] == '\0') {
            continue;
        }
        if (str_eq(t, "|")) {
            if (have_pipe) {
                return -1;
            }
            have_pipe = 1;
            cur = &right;
            continue;
        }
        if (str_eq(t, "<")) {
            if (i + 1 >= nt || !toks[i + 1] || toks[i + 1][0] == '\0') {
                return -1;
            }
            i++;
            resolve_path(toks[i], cur->in_path, (int)sizeof(cur->in_path));
            continue;
        }
        if (str_eq(t, ">")) {
            if (i + 1 >= nt || !toks[i + 1] || toks[i + 1][0] == '\0') {
                return -1;
            }
            i++;
            resolve_path(toks[i], cur->out_path, (int)sizeof(cur->out_path));
            continue;
        }
        if (cur->argc < SH_ARG_MAX) {
            if (cur->argc == 0) {
                sanitize_cmd_token(t);
            }
            cur->argv[cur->argc++] = t;
            cur->argv[cur->argc] = 0;
        }
    }

    if (left.argc <= 0) {
        return -1;
    }
    if (have_pipe && right.argc <= 0) {
        return -1;
    }

    if (!have_pipe) {
        return cmd_run_with_redir(&left, 0);
    }

    if (left.out_path[0] != '\0') {
        return -1;
    }
    for (int i = 0; SH_PIPE_TMP[i] != '\0' && i + 1 < SH_PATH_MAX; i++) {
        left.out_path[i] = SH_PIPE_TMP[i];
        left.out_path[i + 1] = '\0';
    }
    if (right.in_path[0] != '\0') {
        return -1;
    }
    for (int i = 0; SH_PIPE_TMP[i] != '\0' && i + 1 < SH_PATH_MAX; i++) {
        right.in_path[i] = SH_PIPE_TMP[i];
        right.in_path[i + 1] = '\0';
    }

    if (cmd_run_with_redir(&left, 0) < 0) {
        return -1;
    }
    return cmd_run_with_redir(&right, 0);
}

int cmd_run(int argc, char** argv, int verbose)
{
    int status = 0;
    const char* path = (argc >= 1) ? argv[0] : 0;
    const char* spawn_path = path;
    const char* spawn_argv[SH_ARG_MAX + 1];
    char resolved[SH_PATH_MAX];
    if (!path || path[0] == '\0') {
        (void)write_str("run: path required\n");
        return -1;
    }
    for (int i = 0; i < SH_ARG_MAX + 1; i++) {
        spawn_argv[i] = 0;
    }
    for (int i = 0; i < argc && i < SH_ARG_MAX; i++) {
        spawn_argv[i] = argv[i];
    }
    if (argc < SH_ARG_MAX) {
        spawn_argv[argc] = 0;
    }
    if (!path || path[0] != '/') {
        resolve_path(path, resolved, (int)sizeof(resolved));
        spawn_path = resolved;
        spawn_argv[0] = resolved;
    }
    long pid = posix_spawn(spawn_path, spawn_argv);
    if (pid < 0) {
        return -1;
    }
    if (verbose) {
        (void)write_str("run: pid=");
        write_u64((uint64_t)pid);
        (void)write_str("\n");
    }
    {
        long wr = waitpid((pid_t)pid, &status, 0);
        if (wr != pid) {
            (void)write_str("run: waitpid failed\n");
            return -1;
        }
    }
    if (verbose) {
        (void)write_str("run: exit=");
        write_u64((uint64_t)(uint32_t)status);
        (void)write_str("\n");
    }
    if (status != 0) {
        return -1;
    }
    return 0;
}

int cmd_cd(int argc, char** argv)
{
    DIR* d;
    char resolved[SH_PATH_MAX];
    const char* in = "/";
    if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
        in = argv[1];
    }
    resolve_path(in, resolved, (int)sizeof(resolved));
    d = opendir(resolved);
    if (!d) {
        (void)write_str("cd: no such directory\n");
        return -1;
    }
    (void)closedir(d);
    for (int i = 0; resolved[i] != '\0' && i + 1 < SH_PATH_MAX; i++) {
        shell_cwd[i] = resolved[i];
        shell_cwd[i + 1] = '\0';
    }
    return 0;
}

int cmd_autorun(int argc, char** argv)
{
    char path_buf[SH_PATH_MAX];
    char resolved[SH_PATH_MAX];
    if (argc <= 0 || !argv || !argv[0]) {
        return -1;
    }

    int has_slash = 0;
    for (int i = 0; argv[0][i] != '\0'; i++) {
        if (argv[0][i] == '/') {
            has_slash = 1;
            break;
        }
    }

    if (has_slash) {
        resolve_path(argv[0], resolved, (int)sizeof(resolved));
        argv[0] = resolved;
        return cmd_run(argc, argv, 0);
    }

    int p = 0;
    const char* prefix = "/bin/";
    while (prefix[p] != '\0' && p + 1 < (int)sizeof(path_buf)) {
        path_buf[p] = prefix[p];
        p++;
    }
    for (int i = 0; argv[0][i] != '\0' && p + 1 < (int)sizeof(path_buf); i++) {
        path_buf[p++] = argv[0][i];
    }
    path_buf[p] = '\0';

    argv[0] = path_buf;
    return cmd_run(argc, argv, 0);
}
