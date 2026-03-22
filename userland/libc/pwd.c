/*
 * pwd.c — minimal /etc/passwd parser for RodNIX userland.
 * Format: user:password:uid:gid:comment:home:shell
 *
 * Returns a pointer to a static struct; not re-entrant.
 * Reads /mnt/etc/passwd first, falls back to /etc/passwd.
 */

#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PASSWD_BUF_MAX 4096
#define FIELD_MAX 128

static char s_name[FIELD_MAX];
static char s_passwd[FIELD_MAX];
static char s_gecos[FIELD_MAX];
static char s_dir[FIELD_MAX];
static char s_shell[FIELD_MAX];
static struct passwd s_pwd;

static int read_passwd_file(char* buf, int bufsz)
{
    int fd = open(PASSWD_PATH_MNT, O_RDONLY);
    if (fd < 0) {
        fd = open(PASSWD_PATH, O_RDONLY);
    }
    if (fd < 0) {
        return -1;
    }
    int n = (int)read(fd, buf, (size_t)(bufsz - 1));
    close(fd);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';
    return n;
}

static int next_field_s(const char** src, char* dst, int dstsz)
{
    const char* p = *src;
    int i = 0;

    while (*p && *p != ':' && *p != '\n' && i < dstsz - 1) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
    if (*p == ':') {
        p++;
    }
    *src = p;
    return i;
}

static uid_t parse_uid(const char* s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10u) + (uint32_t)(*s - '0');
        s++;
    }
    return (uid_t)v;
}

static gid_t parse_gid(const char* s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10u) + (uint32_t)(*s - '0');
        s++;
    }
    return (gid_t)v;
}

static int parse_line(const char* line)
{
    char uid_s[16];
    char gid_s[16];
    const char* p = line;

    if (!*p || *p == '#') {
        return 0;
    }

    next_field_s(&p, s_name, sizeof(s_name));
    next_field_s(&p, s_passwd, sizeof(s_passwd));
    next_field_s(&p, uid_s, sizeof(uid_s));
    next_field_s(&p, gid_s, sizeof(gid_s));
    next_field_s(&p, s_gecos, sizeof(s_gecos));
    next_field_s(&p, s_dir, sizeof(s_dir));
    next_field_s(&p, s_shell, sizeof(s_shell));

    s_pwd.pw_name = s_name;
    s_pwd.pw_passwd = s_passwd;
    s_pwd.pw_uid = parse_uid(uid_s);
    s_pwd.pw_gid = parse_gid(gid_s);
    s_pwd.pw_change = 0;
    s_pwd.pw_class = (char*)"";
    s_pwd.pw_gecos = s_gecos;
    s_pwd.pw_dir = s_dir;
    s_pwd.pw_shell = s_shell;
    s_pwd.pw_expire = 0;
    return 1;
}

struct passwd* getpwuid(uid_t uid)
{
    static char buf[PASSWD_BUF_MAX];
    if (read_passwd_file(buf, sizeof(buf)) < 0) {
        return NULL;
    }

    const char* line = buf;
    while (*line) {
        const char* end = line;
        char scratch[256];
        int llen;

        while (*end && *end != '\n') {
            end++;
        }
        llen = (int)(end - line);
        if (llen >= (int)sizeof(scratch)) {
            llen = (int)sizeof(scratch) - 1;
        }
        memcpy(scratch, line, (size_t)llen);
        scratch[llen] = '\0';

        if (parse_line(scratch) && s_pwd.pw_uid == uid) {
            return &s_pwd;
        }
        line = (*end == '\n') ? end + 1 : end;
    }
    return NULL;
}

struct passwd* getpwnam(const char* name)
{
    static char buf[PASSWD_BUF_MAX];
    if (!name || read_passwd_file(buf, sizeof(buf)) < 0) {
        return NULL;
    }

    const char* line = buf;
    while (*line) {
        const char* end = line;
        char scratch[256];
        int llen;

        while (*end && *end != '\n') {
            end++;
        }
        llen = (int)(end - line);
        if (llen >= (int)sizeof(scratch)) {
            llen = (int)sizeof(scratch) - 1;
        }
        memcpy(scratch, line, (size_t)llen);
        scratch[llen] = '\0';

        if (parse_line(scratch) && strcmp(s_pwd.pw_name, name) == 0) {
            return &s_pwd;
        }
        line = (*end == '\n') ? end + 1 : end;
    }
    return NULL;
}
