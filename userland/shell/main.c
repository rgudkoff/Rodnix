#include "shell_internal.h"

int main(int argc, char** argv, char** envp)
{
    char line[SH_LINE_MAX];
    char* cmd_argv[SH_ARG_MAX + 1];
    (void)argc;
    (void)argv;

    shell_env_init(envp);

    (void)write_str(SH_ANSI_CLEAR);
    (void)write_str(SH_ANSI_BOTTOM);

    for (;;) {
        (void)write_str(SH_ANSI_BOTTOM);
        shell_print_prompt();
        int len = read_line(line, (int)sizeof(line));
        if (len < 0) {
            (void)write_str("sh: read error\n");
            continue;
        }
        if (len == 0) {
            continue;
        }
        if (line_has_meta(line)) {
            if (cmd_exec_meta_line(line) < 0) {
                (void)write_str("sh: command not found or failed\n");
            }
            continue;
        }

        int cmd_argc = parse_line(line, cmd_argv, SH_ARG_MAX);
        if (cmd_argc <= 0) {
            continue;
        }
        sanitize_cmd_token(cmd_argv[0]);
        if (!cmd_argv[0] || cmd_argv[0][0] == '\0') {
            continue;
        }
        if (str_eq(cmd_argv[0], "help")) {
            cmd_help();
        } else if (str_eq(cmd_argv[0], "pid")) {
            long pid = posix_getpid();
            (void)write_str("pid=");
            if (pid < 0) {
                (void)write_str("ERR\n");
            } else {
                write_u64((uint64_t)pid);
                (void)write_str("\n");
            }
        } else if (str_eq(cmd_argv[0], "hostname")) {
            cmd_hostname();
        } else if (str_eq(cmd_argv[0], "set")) {
            if (cmd_argc == 1) {
                (void)write_str("PS1=");
                (void)write_str(shell_ps1);
                (void)write_str("\n");
            } else if (str_eq(cmd_argv[1], "PS1") && cmd_argc >= 3) {
                (void)shell_set_ps1_from_args(cmd_argc, cmd_argv, 2);
            } else if (str_starts(cmd_argv[1], "PS1=")) {
                char* fake[2];
                fake[0] = cmd_argv[0];
                fake[1] = cmd_argv[1] + 4;
                (void)shell_set_ps1_from_args(2, fake, 1);
            } else {
                (void)write_str("sh: set: usage: set PS1=<prompt>\n");
            }
        } else if (str_eq(cmd_argv[0], "export")) {
            if (shell_env_export(cmd_argc, cmd_argv) != 0) {
                (void)write_str("sh: export: usage: export NAME=value\n");
            }
        } else if (str_eq(cmd_argv[0], "env")) {
            shell_env_print();
        } else if (str_eq(cmd_argv[0], "cd")) {
            (void)cmd_cd(cmd_argc, cmd_argv);
        } else if (str_eq(cmd_argv[0], "motd")) {
            char* av[3];
            av[0] = "/bin/cat";
            av[1] = "/etc/motd";
            av[2] = 0;
            (void)cmd_run(2, av, 0);
        } else if (str_eq(cmd_argv[0], "uname")) {
            cmd_uname();
        } else if (str_eq(cmd_argv[0], "hostinfo") || str_eq(cmd_argv[0], "sysinfo")) {
            (void)cmd_hostinfo();
        } else if (str_eq(cmd_argv[0], "syscalltest")) {
            char* av[2];
            av[0] = "/bin/syscalltest";
            av[1] = 0;
            if (cmd_run(1, av, 0) < 0) {
                (void)write_str("syscalltest: failed\n");
            }
        } else if (str_eq(cmd_argv[0], "ttyreadtest")) {
            char* av[2];
            av[0] = "/bin/ttyreadtest";
            av[1] = 0;
            if (cmd_run(1, av, 0) < 0) {
                (void)write_str("ttyreadtest: failed\n");
            }
        } else if (str_eq(cmd_argv[0], "timecheck")) {
            char* av[2];
            av[0] = "/bin/timecheck";
            av[1] = 0;
            if (cmd_run(1, av, 0) < 0) {
                (void)write_str("timecheck: failed\n");
            }
        } else if (str_eq(cmd_argv[0], "ls")) {
            char* av[SH_ARG_MAX + 1];
            int avc = 0;
            av[avc++] = "/bin/ls";
            for (int i = 1; i < cmd_argc && avc < SH_ARG_MAX; i++) {
                av[avc++] = cmd_argv[i];
            }
            av[avc] = 0;
            (void)cmd_run(avc, av, 0);
        } else if (str_eq(cmd_argv[0], "smoke")) {
            run_smoke();
        } else if (str_eq(cmd_argv[0], "ttytest")) {
            cmd_ttytest();
        } else if (str_eq(cmd_argv[0], "run")) {
            if (cmd_argc < 2) {
                (void)write_str("sh: run: usage: run <path> [args ...]\n");
            } else {
                if (cmd_run(cmd_argc - 1, &cmd_argv[1], 1) < 0) {
                    (void)write_str("sh: run: ");
                    (void)write_str(cmd_argv[1]);
                    (void)write_str(": not found\n");
                }
            }
        } else if (str_eq(cmd_argv[0], "exec")) {
            if (cmd_argc < 2) {
                (void)write_str("sh: exec: usage: exec <path> [args ...]\n");
            } else {
                if (cmd_exec_replace(cmd_argc - 1, &cmd_argv[1]) < 0) {
                    (void)write_str("sh: exec: ");
                    (void)write_str(cmd_argv[1]);
                    (void)write_str(": not found\n");
                }
            }
        } else if (str_eq(cmd_argv[0], "reexec")) {
            if (cmd_argc < 2) {
                (void)write_str("sh: reexec: usage: reexec <path> [args ...]\n");
            } else {
                if (cmd_exec_replace(cmd_argc - 1, &cmd_argv[1]) < 0) {
                    (void)write_str("sh: reexec: ");
                    (void)write_str(cmd_argv[1]);
                    (void)write_str(": not found\n");
                }
            }
        } else if (str_eq(cmd_argv[0], "exit")) {
            (void)write_str("shell exiting\n");
            (void)posix_exit(0);
        } else {
            int rc = cmd_autorun(cmd_argc, cmd_argv);
            if (rc < 0 || rc == 127) {
                (void)write_str("sh: ");
                (void)write_str(cmd_argv[0]);
                (void)write_str(": not found\n");
            }
        }
    }

    for (;;) {
        (void)rdnx_syscall0(SYS_NOP);
    }
    return 0;
}
