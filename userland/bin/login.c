/*
 * Minimal login utility for RodNIX.
 * /etc/passwd format:
 *   user:password:uid:gid:comment:home:shell
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sha256.h>
#include "posix_syscall.h"

/* Persistent passwd on ext2 takes priority over initrd copy */
#define PASSWD_PATH_MNT "/mnt/etc/passwd"
#define PASSWD_PATH     "/etc/passwd"
#define GROUP_PATH_MNT  "/mnt/etc/group"
#define GROUP_PATH      "/etc/group"
#define PASSWD_MAX    4096
#define GROUP_MAX     4096
#define LINE_MAX      256
#define NAME_MAX_LEN  64
#define PASS_HASH_MAX_LEN 72
#define PASS_INPUT_MAX_LEN 64
#define SHELL_MAX_LEN 64
#define HOME_MAX_LEN  64
#define MAX_RETRIES   3
#define LOGIN_MAX_GROUPS 16

typedef struct login_user {
    uint32_t uid;
    uint32_t gid;
    char home[HOME_MAX_LEN];
    char shell[SHELL_MAX_LEN];
} login_user_t;

static long write_str(const char* s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return write(STDOUT_FILENO, s, n);
}

static void secure_bzero(void* ptr, size_t len)
{
    volatile unsigned char* p = (volatile unsigned char*)ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

static ssize_t login_read_byte(int fd, char* c)
{
    if (fd == STDIN_FILENO) {
        long n;

        /* Keep login on the same TTY read path as /bin/sh.
         * The regular fast syscall route is still unreliable for blocking
         * interactive console reads, which can prevent the password prompt
         * flow from advancing after "login:". */
        __asm__ volatile (
            "int $0x80"
            : "=a"(n)
            : "a"(POSIX_SYS_READ), "D"(fd), "S"((long)(uintptr_t)c), "d"(1L)
            : "memory"
        );
        return (ssize_t)n;
    }

    return read(fd, c, 1);
}

static int read_line(int fd, char* buf, int maxlen)
{
    int i = 0;
    char c = '\0';

    if (!buf || maxlen <= 1) {
        return -1;
    }

    while (i < maxlen - 1) {
        ssize_t r = login_read_byte(fd, &c);
        if (r <= 0) {
            break;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

static ssize_t read_passwd_file(char* buf, size_t bufsz)
{
    int fd;
    size_t off = 0;

    if (!buf || bufsz < 2) {
        return -1;
    }

    fd = open(PASSWD_PATH_MNT, O_RDONLY);
    if (fd < 0) {
        fd = open(PASSWD_PATH, O_RDONLY);
    }
    if (fd < 0) {
        return -1;
    }

    while (off + 1 < bufsz) {
        ssize_t n = read(fd, buf + off, bufsz - off - 1);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) {
            break;
        }
        off += (size_t)n;
    }
    close(fd);
    buf[off] = '\0';
    return (ssize_t)off;
}

static ssize_t read_group_file(char* buf, size_t bufsz)
{
    int fd;
    size_t off = 0;

    if (!buf || bufsz < 2) {
        return -1;
    }

    fd = open(GROUP_PATH_MNT, O_RDONLY);
    if (fd < 0) {
        fd = open(GROUP_PATH, O_RDONLY);
    }
    if (fd < 0) {
        return -1;
    }

    while (off + 1 < bufsz) {
        ssize_t n = read(fd, buf + off, bufsz - off - 1);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) {
            break;
        }
        off += (size_t)n;
    }
    close(fd);
    buf[off] = '\0';
    return (ssize_t)off;
}

static int next_field(const char** src, char* buf, int buflen)
{
    const char* p = *src;
    int i = 0;

    if (!src || !*src || !buf || buflen <= 0) {
        return 0;
    }

    while (*p != '\0' && *p != ':' && i < buflen - 1) {
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    if (*p == ':') {
        p++;
    }
    *src = p;
    return i;
}

static int str_eq(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int copy_str(char* dst, size_t dst_size, const char* src)
{
    size_t i = 0;

    if (!dst || !src || dst_size == 0) {
        return -1;
    }
    while (src[i] != '\0') {
        if (i + 1 >= dst_size) {
            return -1;
        }
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return 0;
}

static const char* path_basename(const char* path)
{
    const char* last = path;
    if (!path) {
        return "";
    }
    while (*path != '\0') {
        if (*path == '/') {
            last = path + 1;
        }
        path++;
    }
    return last;
}

static uint32_t str_to_u32(const char* s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10u) + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

static int username_in_group_members(const char* members, const char* username)
{
    const char* p = members;

    if (!members || !username || username[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        char member[NAME_MAX_LEN];
        int i = 0;

        while (*p == ',' || *p == ' ' || *p == '\t') {
            p++;
        }
        while (*p != '\0' && *p != ',' && i < NAME_MAX_LEN - 1) {
            member[i++] = *p++;
        }
        member[i] = '\0';
        if (member[0] != '\0' && str_eq(member, username)) {
            return 1;
        }
        while (*p != '\0' && *p != ',') {
            p++;
        }
        if (*p == ',') {
            p++;
        }
    }
    return 0;
}

static int load_supplementary_groups(const char* username, uint32_t primary_gid,
                                     uint32_t* out_groups, uint32_t* out_count)
{
    char buf[GROUP_MAX];
    ssize_t n;
    const char* line;
    uint32_t count = 0;

    if (!username || !out_groups || !out_count) {
        return -1;
    }

    *out_count = 0;
    n = read_group_file(buf, sizeof(buf));
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }

    line = buf;
    while (*line != '\0') {
        const char* end = line;
        char scratch[LINE_MAX];
        int llen;
        int i;

        while (*end != '\0' && *end != '\n') {
            end++;
        }
        llen = (int)(end - line);
        if (llen >= LINE_MAX) {
            llen = LINE_MAX - 1;
        }
        for (i = 0; i < llen; i++) {
            scratch[i] = line[i];
        }
        scratch[i] = '\0';

        if (scratch[0] != '\0' && scratch[0] != '#') {
            const char* p = scratch;
            char f_name[NAME_MAX_LEN];
            char f_pass[PASS_HASH_MAX_LEN];
            char f_gid[16];
            char f_members[LINE_MAX];
            uint32_t gid;

            next_field(&p, f_name, sizeof(f_name));
            next_field(&p, f_pass, sizeof(f_pass));
            next_field(&p, f_gid, sizeof(f_gid));
            next_field(&p, f_members, sizeof(f_members));
            gid = str_to_u32(f_gid);
            if (gid != primary_gid &&
                username_in_group_members(f_members, username)) {
                if (count >= LOGIN_MAX_GROUPS) {
                    return -1;
                }
                out_groups[count++] = gid;
            }
        }

        line = (*end == '\n') ? (end + 1) : end;
    }

    *out_count = count;
    return 0;
}

/* password_enabled and verification delegated to rdnx_pw_verify (sha256.h) */

static int passwd_lookup(const char* username,
                         const char* password,
                         login_user_t* out_user)
{
    char buf[PASSWD_MAX];
    ssize_t n;
    const char* line;

    if (!username || !password || !out_user) {
        return -1;
    }

    n = read_passwd_file(buf, sizeof(buf));
    if (n <= 0) {
        return -1;
    }

    line = buf;
    while (*line != '\0') {
        const char* end = line;
        char scratch[LINE_MAX];
        int llen;
        int i;

        while (*end != '\0' && *end != '\n') {
            end++;
        }

        llen = (int)(end - line);
        if (llen >= LINE_MAX) {
            llen = LINE_MAX - 1;
        }
        for (i = 0; i < llen; i++) {
            scratch[i] = line[i];
        }
        scratch[i] = '\0';

        if (scratch[0] != '\0' && scratch[0] != '#') {
            const char* p = scratch;
            char f_user[NAME_MAX_LEN];
            char f_pass[PASS_HASH_MAX_LEN];
            char f_uid[16];
            char f_gid[16];
            char f_comment[64];
            char f_home[HOME_MAX_LEN];
            char f_shell[SHELL_MAX_LEN];

            next_field(&p, f_user, sizeof(f_user));
            next_field(&p, f_pass, sizeof(f_pass));
            next_field(&p, f_uid, sizeof(f_uid));
            next_field(&p, f_gid, sizeof(f_gid));
            next_field(&p, f_comment, sizeof(f_comment));
            next_field(&p, f_home, sizeof(f_home));
            next_field(&p, f_shell, sizeof(f_shell));

            if (str_eq(f_user, username)) {
                if (!rdnx_pw_verify(password, f_pass)) {
                    return 0;
                }
                out_user->uid = str_to_u32(f_uid);
                out_user->gid = str_to_u32(f_gid);
                if (copy_str(out_user->home,
                             sizeof(out_user->home),
                             (f_home[0] != '\0') ? f_home : "/") != 0) {
                    return -1;
                }
                if (copy_str(out_user->shell,
                             sizeof(out_user->shell),
                             (f_shell[0] != '\0') ? f_shell : "/bin/sh") != 0) {
                    return -1;
                }
                return 1;
            }
        }

        line = (*end == '\n') ? (end + 1) : end;
    }

    return 0;
}

static int disable_echo(struct termios* saved)
{
    struct termios tio;

    if (!saved) {
        return -1;
    }
    if (tcgetattr(STDIN_FILENO, saved) != 0) {
        return -1;
    }
    tio = *saved;
    tio.c_lflag &= ~(unsigned int)ECHO;
    return tcsetattr(STDIN_FILENO, TCSANOW, &tio);
}

static void restore_echo(const struct termios* saved)
{
    if (saved) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, saved);
    }
}

static long set_gid_full(uint32_t gid)
{
    long rc = rdnx_syscall1(POSIX_SYS_SETGID, (long)gid);
    if (rc < 0) {
        return rc;
    }
    return rdnx_syscall1(POSIX_SYS_SETEGID, (long)gid);
}

static long set_uid_full(uint32_t uid)
{
    long rc = rdnx_syscall1(POSIX_SYS_SETUID, (long)uid);
    if (rc < 0) {
        return rc;
    }
    return rdnx_syscall1(POSIX_SYS_SETEUID, (long)uid);
}

int main(void)
{
    char username[NAME_MAX_LEN];
    char password[PASS_INPUT_MAX_LEN];
    uint32_t supp_groups[LOGIN_MAX_GROUPS];
    uint32_t supp_group_count = 0;
    login_user_t user;

    write_str("\nRodNIX login\n");

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        struct termios saved_tio;
        int echo_disabled = 0;
        int found;

        username[0] = '\0';
        password[0] = '\0';

        write_str("login: ");
        if (read_line(STDIN_FILENO, username, sizeof(username)) <= 0) {
            continue;
        }

        write_str("Password: ");
        if (isatty(STDIN_FILENO) && disable_echo(&saved_tio) == 0) {
            echo_disabled = 1;
        }
        (void)read_line(STDIN_FILENO, password, sizeof(password));
        if (echo_disabled) {
            restore_echo(&saved_tio);
        }
        write_str("\n");

        memset(&user, 0, sizeof(user));
        found = passwd_lookup(username, password, &user);
        secure_bzero(password, sizeof(password));

        if (found < 0) {
            write_str("login: could not read /etc/passwd\n");
            return 1;
        }
        if (found == 0) {
            write_str("Login incorrect\n");
            continue;
        }

        if (chdir(user.home) != 0) {
            (void)chdir("/");
        }
        if (load_supplementary_groups(username, user.gid, supp_groups, &supp_group_count) != 0) {
            write_str("login: group setup failed\n");
            return 1;
        }
        if (setgroups((size_t)supp_group_count, (const gid_t*)supp_groups) != 0) {
            write_str("login: supplementary groups failed\n");
            return 1;
        }
        if (set_gid_full(user.gid) < 0 || set_uid_full(user.uid) < 0) {
            write_str("login: credential switch failed\n");
            return 1;
        }

        {
            char home_env[HOME_MAX_LEN + 5];      /* HOME=...  */
            char user_env[NAME_MAX_LEN + 5];      /* USER=...  */
            char shell_env[SHELL_MAX_LEN + 6];    /* SHELL=... */
            char logname_env[NAME_MAX_LEN + 8];   /* LOGNAME=... */
            char login_argv0[SHELL_MAX_LEN + 2];
            const char* selected_shell = user.shell;
            const char* shell_base;

            if (access(selected_shell, X_OK) != 0) {
                if (!str_eq(selected_shell, "/bin/sh")) {
                    write_str("login: configured shell unavailable, falling back to /bin/sh\n");
                }
                selected_shell = "/bin/sh";
            }
            shell_base = path_basename(selected_shell);

            if (copy_str(home_env, sizeof(home_env), "HOME=") != 0 ||
                copy_str(user_env, sizeof(user_env), "USER=") != 0 ||
                copy_str(shell_env, sizeof(shell_env), "SHELL=") != 0 ||
                copy_str(logname_env, sizeof(logname_env), "LOGNAME=") != 0) {
                write_str("login: environment setup failed\n");
                return 1;
            }
            if (copy_str(home_env + 5, sizeof(home_env) - 5, user.home) != 0 ||
                copy_str(user_env + 5, sizeof(user_env) - 5, username) != 0 ||
                copy_str(shell_env + 6, sizeof(shell_env) - 6, selected_shell) != 0 ||
                copy_str(logname_env + 8, sizeof(logname_env) - 8, username) != 0) {
                write_str("login: environment setup failed\n");
                return 1;
            }

            /* argv[0] with leading '-' signals login-shell mode to standard sh.
             * BusyBox ash also respects this convention after multicall dispatch. */
            copy_str(login_argv0, sizeof(login_argv0), "-sh");
            if (str_eq(shell_base, "busybox")) {
                char* const bb_argv[] = { (char*)"sh", (char*)"-l", (char*)0 };
                char* const bb_envp[] = {
                    home_env,
                    user_env,
                    shell_env,
                    logname_env,
                    (char*)"PATH=/bin",
                    (char*)"TERM=vt100",
                    (char*)0
                };
                execve(selected_shell, bb_argv, bb_envp);
            } else {
                char* const argv[] = { login_argv0, (char*)0 };
                char* const envp[] = {
                    home_env,
                    user_env,
                    shell_env,
                    logname_env,
                    (char*)"PATH=/bin",
                    (char*)"TERM=vt100",
                    (char*)0
                };
                execve(selected_shell, argv, envp);
            }
        }

        write_str("login: exec failed\n");
        return 1;
    }

    write_str("login: too many failed attempts\n");
    return 1;
}
