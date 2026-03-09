#include "shell_internal.h"

typedef struct sh_exec_spec {
    char* argv[SH_ARG_MAX + 1];
    int argc;
    char in_path[SH_PATH_MAX];
    char out_path[SH_PATH_MAX];
    char err_path[SH_PATH_MAX];
    int out_append;
    int err_append;
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
        if (line[i] == '2' && line[i + 1] == '>') {
            if (line[i + 2] == '>') {
                toks[n++] = (char*)"2>>";
                i += 3;
            } else {
                toks[n++] = (char*)"2>";
                i += 2;
            }
            continue;
        }
        if (line[i] == '|' || line[i] == '<' || line[i] == '>') {
            if (line[i] == '|') {
                toks[n++] = (char*)"|";
            } else if (line[i] == '<') {
                toks[n++] = (char*)"<";
            } else {
                if (line[i + 1] == '>') {
                    toks[n++] = (char*)">>";
                    i += 2;
                    continue;
                }
                toks[n++] = (char*)">";
            }
            i++;
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
    s->err_path[0] = '\0';
    s->out_append = 0;
    s->err_append = 0;
    for (int i = 0; i < SH_ARG_MAX + 1; i++) {
        s->argv[i] = 0;
    }
}

static int shell_save_stdio(int* in_saved, int* out_saved, int* err_saved)
{
    if (!in_saved || !out_saved || !err_saved) {
        return -1;
    }
    *in_saved = dup(FD_STDIN);
    *out_saved = dup(FD_STDOUT);
    *err_saved = dup(FD_STDERR);
    if (*in_saved < 0 || *out_saved < 0 || *err_saved < 0) {
        if (*in_saved >= 0) {
            (void)close(*in_saved);
        }
        if (*out_saved >= 0) {
            (void)close(*out_saved);
        }
        if (*err_saved >= 0) {
            (void)close(*err_saved);
        }
        return -1;
    }
    return 0;
}

static void shell_restore_stdio(int in_saved, int out_saved, int err_saved)
{
    if (in_saved >= 0) {
        (void)dup2(in_saved, FD_STDIN);
        (void)close(in_saved);
    }
    if (out_saved >= 0) {
        (void)dup2(out_saved, FD_STDOUT);
        (void)close(out_saved);
    }
    if (err_saved >= 0) {
        (void)dup2(err_saved, FD_STDERR);
        (void)close(err_saved);
    }
}

static int shell_apply_redir_dup(const char* in_path,
                                 const char* out_path,
                                 const char* err_path,
                                 int out_append,
                                 int err_append)
{
    if (in_path && in_path[0] != '\0') {
        int fd = open(in_path, O_RDONLY);
        if (fd < 0) {
            return -1;
        }
        if (dup2(fd, FD_STDIN) < 0) {
            (void)close(fd);
            return -1;
        }
        (void)close(fd);
    }
    if (out_path && out_path[0] != '\0') {
        int flags = O_WRONLY | O_CREAT | (out_append ? 0 : O_TRUNC);
        int fd = open(out_path, flags);
        if (fd < 0) {
            return -1;
        }
        if (out_append && lseek(fd, 0, SEEK_END) < 0) {
            (void)close(fd);
            return -1;
        }
        if (dup2(fd, FD_STDOUT) < 0) {
            (void)close(fd);
            return -1;
        }
        (void)close(fd);
    }
    if (err_path && err_path[0] != '\0') {
        int flags = O_WRONLY | O_CREAT | (err_append ? 0 : O_TRUNC);
        int fd = open(err_path, flags);
        if (fd < 0) {
            return -1;
        }
        if (err_append && lseek(fd, 0, SEEK_END) < 0) {
            (void)close(fd);
            return -1;
        }
        if (dup2(fd, FD_STDERR) < 0) {
            (void)close(fd);
            return -1;
        }
        (void)close(fd);
    }
    return 0;
}

static int cmd_spawn_raw(int argc, char** argv, long* pid_out)
{
    const char* path = (argc >= 1) ? argv[0] : 0;
    const char* spawn_path = path;
    const char* spawn_argv[SH_ARG_MAX + 1];
    char resolved[SH_PATH_MAX];

    if (!pid_out || !path || path[0] == '\0') {
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

    if (path[0] != '/') {
        resolve_path(path, resolved, (int)sizeof(resolved));
        spawn_path = resolved;
        spawn_argv[0] = resolved;
    }

    /* BusyBox multicall convenience:
     * "/bin/busybox <applet> [args...]" -> exec busybox with argv[0]=<applet>.
     * This avoids relying on busybox's secondary dispatcher path and matches
     * how symlink-style multicall invocation behaves.
     */
    if (spawn_path && str_eq(spawn_path, "/bin/busybox") &&
        argc >= 2 && argv[1] && argv[1][0] != '\0' && argv[1][0] != '-') {
        int out_i = 0;
        for (int in_i = 1; in_i < argc && out_i < SH_ARG_MAX; in_i++, out_i++) {
            spawn_argv[out_i] = argv[in_i];
        }
        if (out_i < SH_ARG_MAX + 1) {
            spawn_argv[out_i] = 0;
        }
    }

    long pid = posix_spawn(spawn_path, spawn_argv);
    if (pid < 0 && path[0] == '/') {
        char norm[SH_PATH_MAX];
        char bin_fallback[SH_PATH_MAX];
        const char* base = path;
        resolve_path(path, norm, (int)sizeof(norm));

        if (!str_eq(norm, spawn_path)) {
            spawn_argv[0] = norm;
            pid = posix_spawn(norm, spawn_argv);
        }

        if (pid < 0) {
            for (int i = 0; norm[i] != '\0'; i++) {
                if (norm[i] == '/' && norm[i + 1] != '\0') {
                    base = &norm[i + 1];
                }
            }
            if (base && base[0] != '\0') {
                int p = 0;
                const char* prefix = "/bin/";
                while (prefix[p] != '\0' && p + 1 < (int)sizeof(bin_fallback)) {
                    bin_fallback[p] = prefix[p];
                    p++;
                }
                for (int i = 0; base[i] != '\0' && p + 1 < (int)sizeof(bin_fallback); i++) {
                    bin_fallback[p++] = base[i];
                }
                bin_fallback[p] = '\0';
                spawn_argv[0] = bin_fallback;
                pid = posix_spawn(bin_fallback, spawn_argv);
            }
        }
    }
    if (pid < 0) {
        return -1;
    }
    *pid_out = pid;
    return 0;
}

static int cmd_spawn_autorun(int argc, char** argv, long* pid_out)
{
    char path_buf[SH_PATH_MAX];
    char* run_argv[SH_ARG_MAX + 1];

    if (argc <= 0 || !argv || !argv[0]) {
        return -1;
    }

    for (int i = 0; i < SH_ARG_MAX + 1; i++) {
        run_argv[i] = 0;
    }
    for (int i = 0; i < argc && i < SH_ARG_MAX; i++) {
        run_argv[i] = argv[i];
    }

    int has_slash = 0;
    for (int i = 0; argv[0][i] != '\0'; i++) {
        if (argv[0][i] == '/') {
            has_slash = 1;
            break;
        }
    }

    if (!has_slash) {
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
        run_argv[0] = path_buf;
    }

    return cmd_spawn_raw(argc, run_argv, pid_out);
}

static int cmd_wait_pid(long pid, int verbose)
{
    int status = 0;
    long wr = waitpid((pid_t)pid, &status, 0);
    if (wr != pid) {
        if (verbose) {
            (void)write_str("run: waitpid failed\n");
        }
        return -2;
    }
    if (verbose) {
        (void)write_str("run: exit=");
        write_u64((uint64_t)(uint32_t)status);
        (void)write_str("\n");
    }
    return (status == 0) ? 0 : 1;
}

static int cmd_run_with_redir(sh_exec_spec_t* spec, int verbose)
{
    int in_saved = -1;
    int out_saved = -1;
    int err_saved = -1;
    long pid = -1;

    if (!spec || spec->argc <= 0 || !spec->argv[0]) {
        return -1;
    }
    if (shell_save_stdio(&in_saved, &out_saved, &err_saved) != 0) {
        return -1;
    }
    if (shell_apply_redir_dup(spec->in_path,
                              spec->out_path,
                              spec->err_path,
                              spec->out_append,
                              spec->err_append) != 0) {
        shell_restore_stdio(in_saved, out_saved, err_saved);
        return -1;
    }
    if (cmd_spawn_autorun(spec->argc, spec->argv, &pid) != 0) {
        shell_restore_stdio(in_saved, out_saved, err_saved);
        return -1;
    }
    shell_restore_stdio(in_saved, out_saved, err_saved);

    if (verbose) {
        (void)write_str("run: pid=");
        write_u64((uint64_t)pid);
        (void)write_str("\n");
    }
    {
        int wr = cmd_wait_pid(pid, verbose);
        return (wr == -2) ? -1 : 0;
    }
}

static int cmd_run_pipeline(sh_exec_spec_t* left, sh_exec_spec_t* right)
{
    int saved_in = -1;
    int saved_out = -1;
    int saved_err = -1;
    int pip[2] = {-1, -1};
    long left_pid = -1;
    long right_pid = -1;
    int rc = -1;

    if (!left || !right || left->argc <= 0 || right->argc <= 0) {
        return -1;
    }
    if (pipe(pip) != 0) {
        return -1;
    }
    if (shell_save_stdio(&saved_in, &saved_out, &saved_err) != 0) {
        (void)close(pip[0]);
        (void)close(pip[1]);
        return -1;
    }

    if (dup2(saved_in, FD_STDIN) < 0 || dup2(saved_out, FD_STDOUT) < 0) {
        goto out;
    }
    if ((left->in_path[0] != '\0' || left->err_path[0] != '\0') &&
        shell_apply_redir_dup(left->in_path, "", left->err_path, 0, left->err_append) != 0) {
        goto out;
    }
    if (dup2(pip[1], FD_STDOUT) < 0) {
        goto out;
    }
    if (cmd_spawn_autorun(left->argc, left->argv, &left_pid) != 0) {
        goto out;
    }
    (void)close(pip[1]);
    pip[1] = -1;

    if (dup2(saved_in, FD_STDIN) < 0 || dup2(saved_out, FD_STDOUT) < 0 || dup2(saved_err, FD_STDERR) < 0) {
        goto out;
    }
    if (dup2(pip[0], FD_STDIN) < 0) {
        goto out;
    }
    if ((right->out_path[0] != '\0' || right->err_path[0] != '\0') &&
        shell_apply_redir_dup("", right->out_path, right->err_path, right->out_append, right->err_append) != 0) {
        goto out;
    }
    if (cmd_spawn_autorun(right->argc, right->argv, &right_pid) != 0) {
        goto out;
    }

    rc = 0;
out:
    shell_restore_stdio(saved_in, saved_out, saved_err);
    if (pip[0] >= 0) {
        (void)close(pip[0]);
    }
    if (pip[1] >= 0) {
        (void)close(pip[1]);
    }

    if (left_pid > 0) {
        int left_rc = cmd_wait_pid(left_pid, 0);
        if (left_rc == -2) {
            rc = -1;
        }
    }
    if (right_pid > 0) {
        int right_rc = cmd_wait_pid(right_pid, 0);
        if (right_rc == -2) {
            rc = -1;
        }
    }

    return rc;
}

int cmd_exec_meta_line(char* line)
{
    char* toks[SH_ARG_MAX * 3];
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
            cur->out_append = 0;
            continue;
        }
        if (str_eq(t, ">>")) {
            if (i + 1 >= nt || !toks[i + 1] || toks[i + 1][0] == '\0') {
                return -1;
            }
            i++;
            resolve_path(toks[i], cur->out_path, (int)sizeof(cur->out_path));
            cur->out_append = 1;
            continue;
        }
        if (str_eq(t, "2>")) {
            if (i + 1 >= nt || !toks[i + 1] || toks[i + 1][0] == '\0') {
                return -1;
            }
            i++;
            resolve_path(toks[i], cur->err_path, (int)sizeof(cur->err_path));
            cur->err_append = 0;
            continue;
        }
        if (str_eq(t, "2>>")) {
            if (i + 1 >= nt || !toks[i + 1] || toks[i + 1][0] == '\0') {
                return -1;
            }
            i++;
            resolve_path(toks[i], cur->err_path, (int)sizeof(cur->err_path));
            cur->err_append = 1;
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
    if (!have_pipe) {
        return cmd_run_with_redir(&left, 0);
    }
    if (right.argc <= 0) {
        return -1;
    }
    if (left.out_path[0] != '\0' || right.in_path[0] != '\0') {
        return -1;
    }
    return cmd_run_pipeline(&left, &right);
}

int cmd_run(int argc, char** argv, int verbose)
{
    long pid = -1;

    if (!argv || argc < 1 || !argv[0] || argv[0][0] == '\0') {
        (void)write_str("sh: run: usage: run <path> [args ...]\n");
        return -1;
    }
    if (cmd_spawn_raw(argc, argv, &pid) != 0) {
        return -1;
    }
    if (verbose) {
        (void)write_str("run: pid=");
        write_u64((uint64_t)pid);
        (void)write_str("\n");
    }
    {
        int wr = cmd_wait_pid(pid, verbose);
        return (wr == -2) ? -1 : 0;
    }
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
        (void)write_str("sh: cd: ");
        (void)write_str(in);
        (void)write_str(": not found\n");
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
    long pid = -1;
    if (cmd_spawn_autorun(argc, argv, &pid) != 0) {
        return -1;
    }
    /* Spawn succeeded: do not misreport wait-path issues as "not found". */
    (void)cmd_wait_pid(pid, 0);
    return 0;
}
