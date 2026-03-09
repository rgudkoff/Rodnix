/*
 * shell.c
 * Minimal standalone userspace shell (/bin/sh).
 */

#include <stdint.h>
#include "syscall.h"
#include "posix_syscall.h"
#include "posix_sysnums.h"
#include "unistd.h"
#include "dirent.h"
#include "sysinfo.h"
#include "hwinfo.h"
#include "fabric_node.h"
#include "netif.h"

#define SYS_NOP 0
#define VFS_OPEN_READ 1

#define FD_STDIN  0
#define FD_STDOUT 1

#define SH_LINE_MAX 128
#define SH_ARG_MAX  16
#define SH_ANSI_CLEAR  "\x1b[2J"
/* Move cursor to last visible row (console clamps oversized row values). */
#define SH_ANSI_BOTTOM "\x1b[999;1H"
#define SH_PATH_MAX 256
#define HOSTINFO_HW_MAX 64
#define HOSTINFO_FABRIC_MAX 128
#define HOSTINFO_NETIF_MAX 16

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

static void write_hex_u32(uint32_t v)
{
    static const char table[] = "0123456789ABCDEF";
    char out[8];
    for (int i = 7; i >= 0; i--) {
        out[i] = table[v & 0x0F];
        v >>= 4;
    }
    (void)write_buf(out, 8);
}

static void write_mem_short(uint64_t bytes)
{
    const uint64_t KB = 1024ULL;
    const uint64_t MB = 1024ULL * 1024ULL;
    if (bytes >= MB) {
        write_u64(bytes / MB);
        (void)write_str(" MB");
        return;
    }
    if (bytes >= KB) {
        write_u64(bytes / KB);
        (void)write_str(" KB");
        return;
    }
    write_u64(bytes);
    (void)write_str(" B");
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

static int str_starts(const char* s, const char* p)
{
    if (!s || !p) {
        return 0;
    }
    while (*p) {
        if (*s != *p) {
            return 0;
        }
        s++;
        p++;
    }
    return 1;
}

static void sanitize_cmd_token(char* s)
{
    if (!s) {
        return;
    }
    int w = 0;
    for (int r = 0; s[r] != '\0'; r++) {
        unsigned char c = (unsigned char)s[r];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.' || c == '/') {
            s[w++] = (char)c;
        }
    }
    s[w] = '\0';
}

static int parse_line(char* line, char** argv, int max_args)
{
    int argc = 0;
    int in_word = 0;

    for (int i = 0; line[i] != '\0' && argc < max_args; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c <= 0x20u) {
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

static char shell_cwd[SH_PATH_MAX] = "/";
static long shell_read_stdin_byte(unsigned char* ch)
{
    long n;
    /* Temporary workaround:
     * interactive stdin read uses int 0x80 because fast syscall path for
     * blocking TTY read is currently unstable (hang / no proper wakeup).
     * Do not switch to fast path until kernel-side tty read contract is fixed.
     */
    __asm__ volatile (
        "int $0x80"
        : "=a"(n)
        : "a"(POSIX_SYS_READ), "D"(FD_STDIN), "S"((long)(uintptr_t)ch), "d"(1L)
        : "memory"
    );
    return n;
}

static int path_is_abs(const char* p)
{
    return p && p[0] == '/';
}

static void path_normalize(const char* in, char* out, int out_sz)
{
    char segs[32][SH_PATH_MAX];
    int seg_count = 0;
    int i = 0;

    if (!in || !out || out_sz < 2) {
        return;
    }

    while (in[i] == '/') {
        i++;
    }
    while (in[i] != '\0') {
        char seg[SH_PATH_MAX];
        int slen = 0;
        while (in[i] != '\0' && in[i] != '/' && slen + 1 < SH_PATH_MAX) {
            seg[slen++] = in[i++];
        }
        seg[slen] = '\0';
        while (in[i] == '/') {
            i++;
        }

        if (slen == 0 || (slen == 1 && seg[0] == '.')) {
            continue;
        }
        if (slen == 2 && seg[0] == '.' && seg[1] == '.') {
            if (seg_count > 0) {
                seg_count--;
            }
            continue;
        }
        if (seg_count < 32) {
            for (int k = 0; k <= slen; k++) {
                segs[seg_count][k] = seg[k];
            }
            seg_count++;
        }
    }

    int p = 0;
    out[p++] = '/';
    for (int s = 0; s < seg_count; s++) {
        int k = 0;
        while (segs[s][k] != '\0') {
            if (p + 1 < out_sz) {
                out[p++] = segs[s][k];
            }
            k++;
        }
        if (s + 1 < seg_count && p + 1 < out_sz) {
            out[p++] = '/';
        }
    }
    if (p <= 0) {
        p = 1;
        out[0] = '/';
    }
    out[p] = '\0';
}

static void resolve_path(const char* in, char* out, int out_sz)
{
    char tmp[SH_PATH_MAX];
    int p = 0;

    if (!in || !out || out_sz < 2) {
        return;
    }

    if (path_is_abs(in)) {
        path_normalize(in, out, out_sz);
        return;
    }

    for (int i = 0; shell_cwd[i] != '\0' && p + 1 < (int)sizeof(tmp); i++) {
        tmp[p++] = shell_cwd[i];
    }
    if (p == 0 || tmp[p - 1] != '/') {
        tmp[p++] = '/';
    }
    for (int i = 0; in[i] != '\0' && p + 1 < (int)sizeof(tmp); i++) {
        tmp[p++] = in[i];
    }
    tmp[p] = '\0';
    path_normalize(tmp, out, out_sz);
}

static int read_line(char* out, int out_len)
{
    int pos = 0;
    unsigned char ch = 0;

    if (!out || out_len <= 1) {
        return -1;
    }

    for (;;) {
        long n = shell_read_stdin_byte(&ch);
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

        /* Ignore non-printable control bytes coming from early TTY input path. */
        if (ch < 0x20u || ch > 0x7Eu) {
            continue;
        }

        if (pos + 1 < out_len) {
            out[pos++] = (char)ch;
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

static int cmd_hostinfo(void)
{
    static hwdev_info_t hw[HOSTINFO_HW_MAX];
    static fabric_node_info_t fn[HOSTINFO_FABRIC_MAX];
    static netif_info_t nif[HOSTINFO_NETIF_MAX];
    static rodnix_sysinfo_t si;
    utsname_t u;
    struct {
        int64_t tv_sec;
        int64_t tv_nsec;
    } ts;
    uint32_t hw_total = 0;
    uint32_t fn_total = 0;
    uint32_t nif_total = 0;
    long hw_n = -1;
    long fn_n = -1;
    long nif_n = -1;

    if (posix_sysinfo(&si) == 0) {
        (void)write_str("Host: ");
        (void)write_str(si.sysname);
        (void)write_str(" ");
        (void)write_str(si.release);
        (void)write_str(" (");
        (void)write_str(si.machine);
        (void)write_str(")\n");

        (void)write_str("Build: ");
        (void)write_str(si.version);
        (void)write_str("\n");

        (void)write_str("Uptime: ");
        write_u64(si.uptime_us / 1000000ULL);
        (void)write_str(".");
        write_u64((si.uptime_us % 1000000ULL) / 1000ULL);
        (void)write_str("s\n");

        (void)write_str("CPU: ");
        (void)write_str(si.cpu_vendor);
        (void)write_str(" | ");
        (void)write_str(si.cpu_model);
        (void)write_str(" | fam/mod/step ");
        write_u64((uint64_t)si.cpu_family);
        (void)write_str("/");
        write_u64((uint64_t)si.cpu_model_id);
        (void)write_str("/");
        write_u64((uint64_t)si.cpu_stepping);
        (void)write_str("\n");

        (void)write_str("Memory: total=");
        write_mem_short(si.mem_total_bytes);
        (void)write_str(" free=");
        write_mem_short(si.mem_free_bytes);
        (void)write_str(" used=");
        write_mem_short(si.mem_used_bytes);
        (void)write_str("\n");

        (void)write_str("IRQ: apic=");
        write_u64((uint64_t)si.apic_available);
        (void)write_str(" ioapic=");
        write_u64((uint64_t)si.ioapic_available);
        (void)write_str("\n");

        (void)write_str("Fabric: buses/drivers/devices/services=");
        write_u64((uint64_t)si.fabric_buses);
        (void)write_str("/");
        write_u64((uint64_t)si.fabric_drivers);
        (void)write_str("/");
        write_u64((uint64_t)si.fabric_devices);
        (void)write_str("/");
        write_u64((uint64_t)si.fabric_services);
        (void)write_str("\n");

        (void)write_str("Syscalls: int80=");
        write_u64(si.syscall_int80_count);
        (void)write_str(" fast=");
        write_u64(si.syscall_fast_count);
        (void)write_str("\n");

        (void)write_str("CPU feat: edx=0x");
        write_hex_u32(si.cpu_features);
        (void)write_str(" ecx=0x");
        write_hex_u32(si.cpu_features_ecx);
        (void)write_str("\n");
        return 0;
    }

    if (posix_uname(&u) != 0) {
        (void)write_str("hostinfo: uname failed\n");
        return -1;
    }
    if (posix_clock_gettime(4, &ts) != 0) {
        (void)write_str("hostinfo: clock_gettime failed\n");
        return -1;
    }

    (void)write_str("Host: ");
    (void)write_str(u.sysname);
    (void)write_str(" ");
    (void)write_str(u.release);
    (void)write_str(" (");
    (void)write_str(u.machine);
    (void)write_str(")\n");

    (void)write_str("Build: ");
    (void)write_str(u.version);
    (void)write_str("\n");

    (void)write_str("Uptime: ");
    write_u64((uint64_t)ts.tv_sec);
    (void)write_str(".");
    write_u64((uint64_t)(ts.tv_nsec / 1000000LL));
    (void)write_str("s\n");
    (void)write_str("hostinfo: fallback summary mode\n");

    hw_n = posix_hwlist(hw, HOSTINFO_HW_MAX, &hw_total);
    if (hw_n >= 0) {
        uint64_t attached = 0;
        uint64_t pci = 0;
        for (long i = 0; i < hw_n; i++) {
            if (hw[i].attached) {
                attached++;
            }
            if (hw[i].is_pci) {
                pci++;
            }
        }
        (void)write_str("Hardware: total=");
        write_u64((uint64_t)hw_total);
        (void)write_str(" attached=");
        write_u64(attached);
        (void)write_str(" pci=");
        write_u64(pci);
        (void)write_str("\n");
    }

    fn_n = posix_fabricls(fn, HOSTINFO_FABRIC_MAX, &fn_total);
    if (fn_n >= 0) {
        uint64_t devices = 0;
        uint64_t services = 0;
        uint64_t subsystems = 0;
        for (long i = 0; i < fn_n; i++) {
            if (str_starts(fn[i].path, "/fabric/devices/")) {
                devices++;
            } else if (str_starts(fn[i].path, "/fabric/services/")) {
                services++;
            } else if (str_starts(fn[i].path, "/fabric/subsystems/")) {
                subsystems++;
            }
        }
        (void)write_str("Fabric: nodes=");
        write_u64((uint64_t)fn_total);
        (void)write_str(" devices=");
        write_u64(devices);
        (void)write_str(" services=");
        write_u64(services);
        (void)write_str(" subsystems=");
        write_u64(subsystems);
        (void)write_str("\n");
    }

    nif_n = posix_netiflist(nif, HOSTINFO_NETIF_MAX, &nif_total);
    if (nif_n >= 0) {
        (void)write_str("Network: interfaces=");
        write_u64((uint64_t)nif_total);
        if (nif_n > 0) {
            (void)write_str(" [");
            for (long i = 0; i < nif_n; i++) {
                (void)write_str(nif[i].name);
                if (i + 1 < nif_n) {
                    (void)write_str(",");
                }
            }
            (void)write_str("]");
        }
        (void)write_str("\n");
    }
    return 0;
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

static int cmd_ls_builtin(const char* path)
{
    DIR* d = opendir(path);
    struct dirent* de;
    if (!d) {
        (void)write_str("ls: opendir failed\n");
        return -1;
    }
    while ((de = readdir(d)) != 0) {
        (void)write_str(de->d_name);
        if (de->d_type == DT_DIR) {
            (void)write_str("/");
        }
        (void)write_str("\n");
    }
    (void)closedir(d);
    return 0;
}

static int cmd_run(int argc, char** argv, int verbose)
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
    if (!path_is_abs(path)) {
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

static int cmd_cd(int argc, char** argv)
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

static int cmd_autorun(int argc, char** argv)
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

static void cmd_help(void)
{
    static const char kHelpText[] =
        "Commands:\n"
        "  help          - show this help\n"
        "  pid           - show current pid\n"
        "  hostname      - print /etc/hostname\n"
        "  cd [path]     - change shell working directory\n"
        "  motd          - print /etc/motd\n"
        "  uname         - show system information\n"
        "  hostinfo      - compact host/system report\n"
        "  scstat [-a]   - syscall stats by number (int80/fast)\n"
        "  forktest      - validate fork + COW semantics\n"
        "  execvetest    - validate execve(argv) path\n"
        "  syscalltest   - compare fast syscall vs int80\n"
        "  ttyreadtest   - blocking stdin read probe\n"
        "  ifconfig      - show network interfaces\n"
        "  ls [path]     - external /bin/ls\n"
        "  cat <path>    - external /bin/cat\n"
        "  smoke         - run basic POSIX smoke check\n"
        "  ttytest       - interactive tty line test\n"
        "  run <path>    - spawn program and wait for exit\n"
        "  exec <path>   - exec another program\n"
        "  exit          - terminate shell process\n";
    (void)write_buf(kHelpText, sizeof(kHelpText) - 1u);
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
