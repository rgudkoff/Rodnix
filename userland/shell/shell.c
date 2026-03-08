/*
 * shell.c
 * Minimal standalone userspace shell (/bin/sh).
 */

#include <stdint.h>
#include "syscall.h"
#include "posix_syscall.h"

#define SYS_NOP 0
#define VFS_OPEN_READ 1

#define FD_STDIN  0
#define FD_STDOUT 1

#define SH_LINE_MAX 128
#define SH_ARG_MAX  16
#define SH_ANSI_CLEAR  "\x1b[2J"
#define SH_ANSI_BOTTOM "\x1b[25;1H"

typedef struct {
    uint32_t abi_version;
    uint32_t size;
} rdnx_abi_header_t;

typedef struct {
    rdnx_abi_header_t hdr;
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[64];
    char machine[32];
} utsname_t;

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

static int str_eq(const char* a, const char* b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int parse_line(char* line, char** argv, int max_args)
{
    int argc = 0;
    int in_word = 0;

    for (int i = 0; line[i] != '\0' && argc < max_args; i++) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (in_word) {
                line[i] = '\0';
                in_word = 0;
            }
        } else if (!in_word) {
            argv[argc++] = &line[i];
            in_word = 1;
        }
    }

    argv[argc] = 0;

    return argc;
}

static int read_line(char* out, int out_len)
{
    int pos = 0;
    char ch = 0;

    if (!out || out_len <= 1) {
        return -1;
    }

    for (;;) {
        long n = posix_read(FD_STDIN, &ch, 1);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return 0; /* EOF */
        }

        if (ch == '\r' || ch == '\n') {
            out[pos] = '\0';
            return pos;
        }

        if (ch == 0x7f || ch == 0x08) {
            if (pos > 0) {
                pos--;
            }
            continue;
        }

        if (pos + 1 < out_len) {
            out[pos++] = ch;
        }
    }
}

static void run_smoke(void)
{
    (void)write_str("[USER] sh: smoke start\n");
    (void)write_str("[USER] sh: getpid=");
    {
        long pid = posix_getpid();
        if (pid < 0) {
            (void)write_str("ERR\n");
        } else {
            write_u64((uint64_t)pid);
            (void)write_str("\n");
        }
    }
    (void)write_str("[USER] sh: smoke done\n");
}

static void cmd_uname(void)
{
    utsname_t u;
    long ret = posix_uname(&u);
    if (ret < 0) {
        (void)write_str("uname: failed\n");
        return;
    }
    (void)write_str(u.sysname);
    (void)write_str(" ");
    (void)write_str(u.release);
    (void)write_str(" ");
    (void)write_str(u.machine);
    (void)write_str("\n");
}

static void cmd_cat(const char* path)
{
    char buf[64];
    if (!path || path[0] == '\0') {
        (void)write_str("cat: path required\n");
        return;
    }
    long fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        (void)write_str("cat: open failed\n");
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

static void cmd_hostname(void)
{
    char buf[64];
    long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
    if (fd < 0) {
        (void)write_str("hostname: open failed\n");
        return;
    }

    long n = posix_read((int)fd, buf, sizeof(buf) - 1);
    (void)posix_close((int)fd);
    if (n <= 0) {
        (void)write_str("hostname: read failed\n");
        return;
    }

    int len = (int)n;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        len--;
    }
    if (len <= 0) {
        (void)write_str("hostname: empty\n");
        return;
    }

    (void)write_buf(buf, (uint64_t)len);
    (void)write_str("\n");
}

static void cmd_ttytest(void)
{
    char line[SH_LINE_MAX];
    char byte[4];

    (void)write_str("ttytest: type a line and press Enter.\n");
    (void)write_str("ttytest: Ctrl-U clears line, Ctrl-D on empty line returns EOF.\n");
    (void)write_str("ttytest> ");

    int n = read_line(line, (int)sizeof(line));
    if (n < 0) {
        (void)write_str("ttytest: read error\n");
        return;
    }
    if (n == 0) {
        (void)write_str("ttytest: empty line/EOF\n");
        return;
    }

    (void)write_str("ttytest: len=");
    write_u64((uint64_t)n);
    (void)write_str(" text='");
    (void)write_buf(line, (uint64_t)n);
    (void)write_str("'\n");

    (void)write_str("ttytest: bytes=");
    for (int i = 0; i < n; i++) {
        byte[0] = ' ';
        byte[1] = "0123456789ABCDEF"[(line[i] >> 4) & 0x0F];
        byte[2] = "0123456789ABCDEF"[line[i] & 0x0F];
        byte[3] = '\0';
        (void)write_str(byte);
    }
    (void)write_str("\n");
}

static int cmd_run(int argc, char** argv, int verbose)
{
    int status = 0;
    const char* path = (argc >= 1) ? argv[0] : 0;
    if (!path || path[0] == '\0') {
        (void)write_str("run: path required\n");
        return -1;
    }
    long pid = posix_spawn(path, (const char* const*)argv);
    if (pid < 0) {
        return -1;
    }
    if (verbose) {
        (void)write_str("run: pid=");
        write_u64((uint64_t)pid);
        (void)write_str("\n");
    }
    long wr = posix_waitpid(pid, &status);
    if (wr < 0) {
        (void)write_str("run: waitpid failed\n");
        return -1;
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

static int cmd_autorun(int argc, char** argv)
{
    char path_buf[SH_LINE_MAX];
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

static void cmd_help(void)
{
    (void)write_str("Commands:\n");
    (void)write_str("  help          - show this help\n");
    (void)write_str("  pid           - show current pid\n");
    (void)write_str("  hostname      - print /etc/hostname\n");
    (void)write_str("  motd          - print /etc/motd\n");
    (void)write_str("  uname         - show system information\n");
    (void)write_str("  ls            - placeholder (readdir syscall pending)\n");
    (void)write_str("  cat <path>    - print file content\n");
    (void)write_str("  smoke         - run basic POSIX smoke check\n");
    (void)write_str("  ttytest       - interactive tty line test\n");
    (void)write_str("  run <path>    - spawn program and wait for exit\n");
    (void)write_str("  exec <path>   - exec another program\n");
    (void)write_str("  exit          - terminate shell process\n");
}

int main(void)
{
    char line[SH_LINE_MAX];
    char* argv[SH_ARG_MAX + 1];

    /* Clear boot logs and position prompt on the last line. */
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

        int argc = parse_line(line, argv, SH_ARG_MAX);
        if (argc <= 0) {
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
        } else if (str_eq(argv[0], "motd")) {
            cmd_cat("/etc/motd");
        } else if (str_eq(argv[0], "uname")) {
            cmd_uname();
        } else if (str_eq(argv[0], "ls")) {
            (void)write_str("ls: not implemented yet in userspace (readdir syscall pending)\n");
        } else if (str_eq(argv[0], "cat")) {
            cmd_cat((argc >= 2) ? argv[1] : 0);
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
                long ret = posix_exec(argv[1]);
                if (ret < 0) {
                    (void)write_str("exec: failed\n");
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
