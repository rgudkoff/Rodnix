#include "shell_internal.h"

int main(void)
{
    char line[SH_LINE_MAX];
    char* argv[SH_ARG_MAX + 1];

    (void)write_str(SH_ANSI_CLEAR);
    (void)write_str(SH_ANSI_BOTTOM);

    for (;;) {
        (void)write_str(SH_ANSI_BOTTOM);
        (void)write_str("sh> ");
        int len = read_line(line, (int)sizeof(line));
        if (len < 0) {
            (void)write_str("read error\n");
            continue;
        }
        if (len == 0) {
            continue;
        }
        if (line_has_meta(line)) {
            if (cmd_exec_meta_line(line) < 0) {
                (void)write_str("command not found or failed\n");
            }
            continue;
        }

        int argc = parse_line(line, argv, SH_ARG_MAX);
        if (argc <= 0) {
            continue;
        }
        sanitize_cmd_token(argv[0]);
        if (!argv[0] || argv[0][0] == '\0') {
            continue;
        }
        if (str_eq(argv[0], "help")) {
            cmd_help();
        } else if (str_eq(argv[0], "pid")) {
            long pid = posix_getpid();
            (void)write_str("pid=");
            if (pid < 0) {
                (void)write_str("ERR\n");
            } else {
                write_u64((uint64_t)pid);
                (void)write_str("\n");
            }
        } else if (str_eq(argv[0], "hostname")) {
            cmd_hostname();
        } else if (str_eq(argv[0], "cd")) {
            (void)cmd_cd(argc, argv);
        } else if (str_eq(argv[0], "motd")) {
            char* av[3];
            av[0] = "/bin/cat";
            av[1] = "/etc/motd";
            av[2] = 0;
            (void)cmd_run(2, av, 0);
        } else if (str_eq(argv[0], "uname")) {
            cmd_uname();
        } else if (str_eq(argv[0], "hostinfo") || str_eq(argv[0], "sysinfo")) {
            (void)cmd_hostinfo();
        } else if (str_eq(argv[0], "syscalltest")) {
            char* av[2];
            av[0] = "/bin/syscalltest";
            av[1] = 0;
            if (cmd_run(1, av, 0) < 0) {
                (void)write_str("syscalltest: failed\n");
            }
        } else if (str_eq(argv[0], "ttyreadtest")) {
            char* av[2];
            av[0] = "/bin/ttyreadtest";
            av[1] = 0;
            if (cmd_run(1, av, 0) < 0) {
                (void)write_str("ttyreadtest: failed\n");
            }
        } else if (str_eq(argv[0], "timecheck")) {
            char* av[2];
            av[0] = "/bin/timecheck";
            av[1] = 0;
            if (cmd_run(1, av, 0) < 0) {
                (void)write_str("timecheck: failed\n");
            }
        } else if (str_eq(argv[0], "ls")) {
            if (argc >= 2 && argv[1]) {
                char path_buf[SH_PATH_MAX];
                resolve_path(argv[1], path_buf, (int)sizeof(path_buf));
                (void)cmd_ls_builtin(path_buf);
            } else {
                (void)cmd_ls_builtin(shell_cwd);
            }
        } else if (str_eq(argv[0], "cat")) {
            char path_buf[SH_PATH_MAX];
            char* av[3];
            if (argc < 2 || !argv[1]) {
                (void)write_str("cat: path required\n");
            } else {
                resolve_path(argv[1], path_buf, (int)sizeof(path_buf));
                av[0] = "/bin/cat";
                av[1] = path_buf;
                av[2] = 0;
                (void)cmd_run(2, av, 0);
            }
        } else if (str_eq(argv[0], "smoke")) {
            run_smoke();
        } else if (str_eq(argv[0], "ttytest")) {
            cmd_ttytest();
        } else if (str_eq(argv[0], "run")) {
            if (argc < 2) {
                (void)write_str("run: path required\n");
            } else {
                if (cmd_run(argc - 1, &argv[1], 1) < 0) {
                    (void)write_str("run: spawn failed\n");
                }
            }
        } else if (str_eq(argv[0], "exec")) {
            if (argc < 2) {
                (void)write_str("exec: path required\n");
            } else {
                if (cmd_run(argc - 1, &argv[1], 1) < 0) {
                    (void)write_str("exec: spawn failed\n");
                }
            }
        } else if (str_eq(argv[0], "reexec")) {
            if (argc < 2) {
                (void)write_str("reexec: path required\n");
            } else {
                char path_buf[SH_PATH_MAX];
                char* ex_argv[SH_ARG_MAX + 1];
                resolve_path(argv[1], path_buf, (int)sizeof(path_buf));
                ex_argv[0] = path_buf;
                int ex_argc = 1;
                for (int i = 2; i < argc && ex_argc < SH_ARG_MAX; i++) {
                    ex_argv[ex_argc++] = argv[i];
                }
                ex_argv[ex_argc] = 0;
                long ret = posix_execve(path_buf, (const char* const*)ex_argv, (const char* const*)0);
                if (ret < 0) {
                    (void)write_str("reexec: failed\n");
                }
            }
        } else if (str_eq(argv[0], "exit")) {
            (void)write_str("shell exiting\n");
            (void)posix_exit(0);
        } else {
            if (cmd_autorun(argc, argv) < 0) {
                (void)write_str("command not found or failed\n");
            }
        }
    }

    for (;;) {
        (void)rdnx_syscall0(SYS_NOP);
    }
    return 0;
}
