/*
 * passwd.c — change user password in /etc/passwd for RodNIX.
 * Format: user:password:uid:gid:comment:home:shell
 *
 * Usage: passwd [username]
 *   Non-root: can only change own password; asks for current password first.
 *   Root: can change any user's password without confirmation.
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

/* Write to persistent ext2 copy; fall back to initrd copy for reads */
#define PASSWD_PATH_MNT "/mnt/etc/passwd"
#define PASSWD_PATH     "/etc/passwd"
#define PASSWD_MAX      4096
#define NAME_MAX_LEN  64
#define PASS_MAX_LEN  64
#define LINE_MAX_LEN  256

static long write_str(const char* s)
{
    size_t n = 0;
    while (s[n] != '\0') n++;
    return write(STDOUT_FILENO, s, n);
}

static int read_line_noecho(int fd, char* buf, int maxlen)
{
    struct termios saved, noecho;
    int echo_ok = 0;
    int n;

    if (isatty(fd) && tcgetattr(fd, &saved) == 0) {
        noecho = saved;
        noecho.c_lflag &= ~(unsigned int)ECHO;
        (void)tcsetattr(fd, TCSANOW, &noecho);
        echo_ok = 1;
    }

    n = 0;
    while (n < maxlen - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) break;
        if (c == '\n' || c == '\r') break;
        buf[n++] = c;
    }
    buf[n] = '\0';

    if (echo_ok) {
        (void)tcsetattr(fd, TCSANOW, &saved);
    }
    write_str("\n");
    return n;
}

static void secure_bzero(void* p, size_t n)
{
    volatile unsigned char* v = (volatile unsigned char*)p;
    while (n-- > 0) *v++ = 0;
}

static int str_eq(const char* a, const char* b)
{
    while (*a && *b) {
        if (*a++ != *b++) return 0;
    }
    return *a == *b;
}

static size_t str_len(const char* s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/*
 * Rebuild /etc/passwd replacing the password field for `target_user`.
 * new_pass is the plaintext new password.
 * Returns 0 on success, -1 on error.
 */
static int rewrite_passwd(const char* target_user, const char* new_pass)
{
    char buf[PASSWD_MAX];
    char out[PASSWD_MAX];
    /* Hash the new password before storing */
    char hashed[6 + 64 + 1]; /* "$rdnx$" + 64 hex + NUL */
    rdnx_pw_hash(new_pass, hashed, sizeof(hashed));

    int fd;
    ssize_t n;
    int found = 0;

    /* Read from persistent copy first, fall back to initrd */
    fd = open(PASSWD_PATH_MNT, O_RDONLY);
    if (fd < 0) fd = open(PASSWD_PATH, O_RDONLY);
    if (fd < 0) {
        write_str("passwd: cannot read /etc/passwd\n");
        return -1;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        write_str("passwd: /etc/passwd is empty\n");
        return -1;
    }
    buf[n] = '\0';

    /* Walk and rebuild line by line */
    size_t out_len = 0;
    const char* line = buf;
    while (*line) {
        const char* end = line;
        while (*end && *end != '\n') end++;

        /* Extract this line */
        char scratch[LINE_MAX_LEN];
        int llen = (int)(end - line);
        if (llen >= LINE_MAX_LEN) llen = LINE_MAX_LEN - 1;
        int i;
        for (i = 0; i < llen; i++) scratch[i] = line[i];
        scratch[i] = '\0';

        /* Check if this is the target user */
        int replaced = 0;
        if (scratch[0] && scratch[0] != '#') {
            /* Find first colon = end of username */
            const char* colon1 = scratch;
            while (*colon1 && *colon1 != ':') colon1++;
            int ulen = (int)(colon1 - scratch);

            char uname[NAME_MAX_LEN];
            if (ulen >= NAME_MAX_LEN) ulen = NAME_MAX_LEN - 1;
            for (i = 0; i < ulen; i++) uname[i] = scratch[i];
            uname[ulen] = '\0';

            if (str_eq(uname, target_user) && *colon1 == ':') {
                /* Skip old password field */
                const char* colon2 = colon1 + 1;
                while (*colon2 && *colon2 != ':') colon2++;

                /* Build new line: username:newpass:rest */
                /* username part */
                if (out_len + (size_t)ulen + 1 >= PASSWD_MAX) goto too_long;
                for (i = 0; i < ulen; i++) out[out_len++] = scratch[i];
                out[out_len++] = ':';
                /* hashed new password */
                size_t plen = str_len(hashed);
                if (out_len + plen + 1 >= PASSWD_MAX) goto too_long;
                for (size_t j = 0; j < plen; j++) out[out_len++] = hashed[j];
                /* rest of line (from colon2 onward) */
                size_t rlen = str_len(colon2);
                if (out_len + rlen + 1 >= PASSWD_MAX) goto too_long;
                for (size_t j = 0; j < rlen; j++) out[out_len++] = colon2[j];
                out[out_len++] = '\n';
                found = 1;
                replaced = 1;
            }
        }

        if (!replaced) {
            /* Copy line verbatim */
            if (out_len + (size_t)llen + 1 >= PASSWD_MAX) goto too_long;
            for (i = 0; i < llen; i++) out[out_len++] = scratch[i];
            out[out_len++] = '\n';
        }

        line = (*end == '\n') ? end + 1 : end;
    }

    if (!found) {
        write_str("passwd: user not found in /etc/passwd\n");
        return -1;
    }

    /* Write to persistent ext2 copy; if unavailable, fall back to initrd */
    fd = open(PASSWD_PATH_MNT, O_WRONLY | O_TRUNC | O_CREAT);
    if (fd < 0) {
        fd = open(PASSWD_PATH, O_WRONLY | O_TRUNC);
    }
    if (fd < 0) {
        write_str("passwd: cannot write /etc/passwd\n");
        return -1;
    }
    ssize_t w = write(fd, out, out_len);
    close(fd);
    if (w != (ssize_t)out_len) {
        write_str("passwd: write failed\n");
        return -1;
    }
    return 0;

too_long:
    write_str("passwd: /etc/passwd too large\n");
    return -1;
}

/*
 * Read current password for `username` from /etc/passwd.
 * Returns 1 if password matches, 0 if wrong, -1 on error.
 */
static int check_current_password(const char* username, const char* password)
{
    char buf[PASSWD_MAX];
    int fd = open(PASSWD_PATH_MNT, O_RDONLY);
    if (fd < 0) fd = open(PASSWD_PATH, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    const char* line = buf;
    while (*line) {
        const char* end = line;
        while (*end && *end != '\n') end++;

        char scratch[LINE_MAX_LEN];
        int llen = (int)(end - line);
        if (llen >= LINE_MAX_LEN) llen = LINE_MAX_LEN - 1;
        int i;
        for (i = 0; i < llen; i++) scratch[i] = line[i];
        scratch[i] = '\0';

        if (scratch[0] && scratch[0] != '#') {
            const char* p = scratch;
            /* username */
            char f_user[NAME_MAX_LEN], f_pass[PASS_MAX_LEN];
            int j = 0;
            while (*p && *p != ':' && j < (int)sizeof(f_user) - 1) f_user[j++] = *p++;
            f_user[j] = '\0';
            if (*p == ':') p++;
            /* password */
            j = 0;
            while (*p && *p != ':' && j < (int)sizeof(f_pass) - 1) f_pass[j++] = *p++;
            f_pass[j] = '\0';

            if (str_eq(f_user, username)) {
                return rdnx_pw_verify(password, f_pass);
            }
        }
        line = (*end == '\n') ? end + 1 : end;
    }
    return -1; /* not found */
}

/*
 * Determine the username for uid by scanning /etc/passwd.
 * Returns 1 on success, 0 if not found.
 */
static int uid_to_username(uint32_t uid, char* out, size_t outsz)
{
    char buf[PASSWD_MAX];
    int fd = open(PASSWD_PATH_MNT, O_RDONLY);
    if (fd < 0) fd = open(PASSWD_PATH, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    const char* line = buf;
    while (*line) {
        const char* end = line;
        while (*end && *end != '\n') end++;

        char scratch[LINE_MAX_LEN];
        int llen = (int)(end - line);
        if (llen >= LINE_MAX_LEN) llen = LINE_MAX_LEN - 1;
        int i;
        for (i = 0; i < llen; i++) scratch[i] = line[i];
        scratch[i] = '\0';

        if (scratch[0] && scratch[0] != '#') {
            const char* p = scratch;
            char f_user[NAME_MAX_LEN], f_pass[PASS_MAX_LEN];
            char f_uid_s[16];
            int j;
            j = 0;
            while (*p && *p != ':' && j < (int)sizeof(f_user)-1) f_user[j++] = *p++;
            f_user[j] = '\0'; if (*p==':') p++;
            j = 0;
            while (*p && *p != ':' && j < (int)sizeof(f_pass)-1) f_pass[j++] = *p++;
            f_pass[j] = '\0'; if (*p==':') p++;
            j = 0;
            while (*p && *p != ':' && j < (int)sizeof(f_uid_s)-1) f_uid_s[j++] = *p++;
            f_uid_s[j] = '\0';

            uint32_t v = 0;
            const char* sp = f_uid_s;
            while (*sp >= '0' && *sp <= '9') { v = v*10u + (uint32_t)(*sp-'0'); sp++; }

            if (v == uid) {
                size_t un = str_len(f_user);
                if (un >= outsz) un = outsz - 1;
                for (size_t k = 0; k < un; k++) out[k] = f_user[k];
                out[un] = '\0';
                return 1;
            }
        }
        line = (*end == '\n') ? end + 1 : end;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    uid_t caller_uid = geteuid();
    int is_root = (caller_uid == 0);

    /* Determine target username */
    char target[NAME_MAX_LEN];
    if (argc >= 2) {
        if (!is_root) {
            write_str("passwd: only root can change another user's password\n");
            return 1;
        }
        size_t len = str_len(argv[1]);
        if (len >= NAME_MAX_LEN) {
            write_str("passwd: username too long\n");
            return 1;
        }
        for (size_t i = 0; i <= len; i++) target[i] = argv[1][i];
    } else {
        if (!uid_to_username((uint32_t)caller_uid, target, sizeof(target))) {
            write_str("passwd: cannot determine current username\n");
            return 1;
        }
    }

    write_str("Changing password for ");
    write_str(target);
    write_str(".\n");

    char cur_pass[PASS_MAX_LEN];
    char new_pass1[PASS_MAX_LEN];
    char new_pass2[PASS_MAX_LEN];

    /* Non-root must verify current password */
    if (!is_root) {
        write_str("Current password: ");
        read_line_noecho(STDIN_FILENO, cur_pass, sizeof(cur_pass));

        int ok = check_current_password(target, cur_pass);
        secure_bzero(cur_pass, sizeof(cur_pass));
        if (ok != 1) {
            write_str("passwd: authentication failure\n");
            return 1;
        }
    }

    /* New password (twice) */
    write_str("New password: ");
    read_line_noecho(STDIN_FILENO, new_pass1, sizeof(new_pass1));

    write_str("Retype new password: ");
    read_line_noecho(STDIN_FILENO, new_pass2, sizeof(new_pass2));

    if (!str_eq(new_pass1, new_pass2)) {
        secure_bzero(new_pass1, sizeof(new_pass1));
        secure_bzero(new_pass2, sizeof(new_pass2));
        write_str("passwd: passwords do not match\n");
        return 1;
    }

    int rc = rewrite_passwd(target, new_pass1);
    secure_bzero(new_pass1, sizeof(new_pass1));
    secure_bzero(new_pass2, sizeof(new_pass2));

    if (rc != 0) return 1;

    write_str("passwd: password updated successfully\n");
    return 0;
}
