#include "shell_internal.h"

static char** g_shell_env = NULL;
static int g_shell_env_count = 0;
static int g_shell_env_cap = 0;

static int shell_env_name_len(const char* entry)
{
    int len = 0;
    if (!entry) {
        return 0;
    }
    while (entry[len] != '\0' && entry[len] != '=') {
        len++;
    }
    return len;
}

static int shell_env_name_valid(const char* name)
{
    if (!name || name[0] == '\0') {
        return 0;
    }
    if (!((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') ||
          name[0] == '_')) {
        return 0;
    }
    for (int i = 1; name[i] != '\0'; i++) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int shell_env_find_index(const char* name)
{
    int nlen;
    if (!name || !g_shell_env) {
        return -1;
    }
    nlen = (int)strlen(name);
    for (int i = 0; i < g_shell_env_count; i++) {
        if (!g_shell_env[i]) {
            continue;
        }
        if (strncmp(g_shell_env[i], name, (size_t)nlen) == 0 &&
            g_shell_env[i][nlen] == '=') {
            return i;
        }
    }
    return -1;
}

static int shell_env_ensure_cap(int want)
{
    char** new_env;
    int new_cap;

    if (g_shell_env_cap >= want) {
        return 0;
    }
    new_cap = (g_shell_env_cap > 0) ? g_shell_env_cap : 8;
    while (new_cap < want) {
        new_cap *= 2;
    }
    new_env = (char**)realloc(g_shell_env, (size_t)(new_cap + 1) * sizeof(char*));
    if (!new_env) {
        return -1;
    }
    for (int i = g_shell_env_cap; i <= new_cap; i++) {
        new_env[i] = NULL;
    }
    g_shell_env = new_env;
    g_shell_env_cap = new_cap;
    environ = g_shell_env;
    return 0;
}

void shell_env_init(char** envp)
{
    char cwd_buf[SH_PATH_MAX];

    g_shell_env = NULL;
    g_shell_env_count = 0;
    g_shell_env_cap = 0;
    environ = NULL;

    if (envp) {
        for (int i = 0; envp[i] != NULL; i++) {
            int name_len = shell_env_name_len(envp[i]);
            if (name_len <= 0 || shell_env_ensure_cap(g_shell_env_count + 1) != 0) {
                continue;
            }
            g_shell_env[g_shell_env_count] = strdup(envp[i]);
            if (!g_shell_env[g_shell_env_count]) {
                continue;
            }
            g_shell_env_count++;
            g_shell_env[g_shell_env_count] = NULL;
        }
    }

    environ = g_shell_env;
    if (!shell_env_get("PATH")) {
        (void)shell_env_set("PATH", "/bin");
    }
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) {
        if (cwd_buf[0] == '\0') {
            cwd_buf[0] = '/';
            cwd_buf[1] = '\0';
        }
        strncpy(shell_cwd, cwd_buf, sizeof(shell_cwd) - 1);
        shell_cwd[sizeof(shell_cwd) - 1] = '\0';
    }
    shell_sync_pwd();
    {
        const char* ps1 = shell_env_get("PS1");
        if (ps1) {
            shell_set_ps1(ps1);
        }
    }
}

const char* shell_env_get(const char* name)
{
    return getenv(name);
}

int shell_env_set(const char* name, const char* value)
{
    size_t nlen;
    size_t vlen;
    char* entry;
    int idx;

    if (!shell_env_name_valid(name)) {
        return -1;
    }
    if (!value) {
        value = "";
    }

    nlen = strlen(name);
    vlen = strlen(value);
    entry = (char*)malloc(nlen + 1 + vlen + 1);
    if (!entry) {
        return -1;
    }
    memcpy(entry, name, nlen);
    entry[nlen] = '=';
    memcpy(entry + nlen + 1, value, vlen + 1);

    idx = shell_env_find_index(name);
    if (idx >= 0) {
        free(g_shell_env[idx]);
        g_shell_env[idx] = entry;
    } else {
        if (shell_env_ensure_cap(g_shell_env_count + 1) != 0) {
            free(entry);
            return -1;
        }
        g_shell_env[g_shell_env_count++] = entry;
        g_shell_env[g_shell_env_count] = NULL;
    }
    environ = g_shell_env;

    if (str_eq(name, "PS1")) {
        shell_set_ps1(value);
    }
    return 0;
}

void shell_sync_pwd(void)
{
    (void)shell_env_set("PWD", shell_cwd);
}

void shell_env_print(void)
{
    for (int i = 0; g_shell_env && i < g_shell_env_count; i++) {
        if (g_shell_env[i]) {
            (void)write_str(g_shell_env[i]);
            (void)write_str("\n");
        }
    }
}

int shell_env_export(int argc, char** argv)
{
    if (argc <= 1) {
        shell_env_print();
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        int eq = -1;
        if (!arg || arg[0] == '\0') {
            continue;
        }
        for (int j = 0; arg[j] != '\0'; j++) {
            if (arg[j] == '=') {
                eq = j;
                break;
            }
        }
        if (eq >= 0) {
            char* value = &arg[eq + 1];
            arg[eq] = '\0';
            if (shell_env_set(arg, value) != 0) {
                arg[eq] = '=';
                return -1;
            }
            arg[eq] = '=';
            continue;
        }
        if (!shell_env_get(arg)) {
            return -1;
        }
        if (str_eq(arg, "PS1")) {
            shell_set_ps1(shell_env_get(arg));
        }
    }
    return 0;
}

int shell_find_executable(const char* cmd, char* out, int out_sz)
{
    const char* path_env;
    char candidate[SH_PATH_MAX];

    if (!cmd || !out || out_sz < 2) {
        return -1;
    }

    for (int i = 0; cmd[i] != '\0'; i++) {
        if (cmd[i] == '/') {
            resolve_path(cmd, out, out_sz);
            return 0;
        }
    }

    path_env = shell_env_get("PATH");
    if (!path_env || path_env[0] == '\0') {
        path_env = "/bin";
    }

    for (int start = 0;;) {
        int end = start;
        int p = 0;
        while (path_env[end] != '\0' && path_env[end] != ':') {
            end++;
        }

        if (end == start) {
            candidate[p++] = '.';
        } else {
            for (int i = start; i < end && p + 1 < (int)sizeof(candidate); i++) {
                candidate[p++] = path_env[i];
            }
        }
        if (p == 0 || candidate[p - 1] != '/') {
            candidate[p++] = '/';
        }
        for (int i = 0; cmd[i] != '\0' && p + 1 < (int)sizeof(candidate); i++) {
            candidate[p++] = cmd[i];
        }
        candidate[p] = '\0';
        resolve_path(candidate, out, out_sz);

        {
            int fd = open(out, O_RDONLY);
            if (fd >= 0) {
                (void)close(fd);
                return 0;
            }
        }

        if (path_env[end] == '\0') {
            break;
        }
        start = end + 1;
    }

    out[0] = '\0';
    return -1;
}

void run_smoke(void)
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

void cmd_uname(void)
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

int cmd_hostinfo(void)
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

void cmd_hostname(void)
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

void cmd_ttytest(void)
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

int cmd_ls_builtin(const char* path)
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

void cmd_help(void)
{
    static const char kHelpText[] =
        "Commands:\n"
        "  help          - show this help\n"
        "  pid           - show current pid\n"
        "  set           - set shell vars, e.g. set PS1=\" # \"\n"
        "  export K=V    - set/export environment variable\n"
        "  env           - print environment\n"
        "  hostname      - print /etc/hostname\n"
        "  cd [path]     - change shell working directory\n"
        "  motd          - print /etc/motd\n"
        "  uname         - show system information\n"
        "  hostinfo      - compact host/system report\n"
        "  diskinfo      - list disks; -r <dev> <lba>; -x <dev|/path> <off> <len>\n"
        "  kmodctl       - module ctl: kmodctl ls|load|unload\n"
        "  scstat [-a]   - syscall stats by number (int80/fast)\n"
        "  forktest      - validate fork + COW semantics\n"
        "  execvetest    - validate execve(argv) path\n"
        "  syscalltest   - compare fast syscall vs int80\n"
        "  ttyreadtest   - blocking stdin read probe\n"
        "  ifconfig      - show network interfaces\n"
        "  udptest       - UDP net0 socket smoke test\n"
        "  ping [ip] [ms]- ICMP echo via kernel net stack\n"
        "  ls [path]     - external /bin/ls\n"
        "  cat <path>    - external /bin/cat\n"
        "  smoke         - run basic POSIX smoke check\n"
        "  ttytest       - interactive tty line test\n"
        "  run <path>    - spawn program and wait for exit\n"
        "  exec <path>   - run program (safe, returns to shell)\n"
        "  reexec <path> - replace shell process via execve\n"
        "  syntax: cmd > file, cmd >> file, cmd 2> file, cmd < file, cmd1 | cmd2\n"
        "  exit          - terminate shell process\n";
    (void)write_buf(kHelpText, sizeof(kHelpText) - 1u);
}
