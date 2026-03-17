/*
 * grp.c — minimal /etc/group parser for RodNIX userland.
 * Format: groupname:password:gid:member1,member2,...
 *
 * Returns a pointer to a static struct; not re-entrant.
 * Reads /mnt/etc/group first, falls back to /etc/group.
 */

#include <grp.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GROUP_BUF_MAX  2048
#define MEMBER_MAX     32
#define FIELD_MAX      64

static char         s_name[FIELD_MAX];
static char         s_passwd[FIELD_MAX];
static char         s_membuf[GROUP_BUF_MAX]; /* raw member string + member ptrs */
static char*        s_members[MEMBER_MAX + 1];
static struct group s_grp;

static int read_group_file(char* buf, int bufsz)
{
    int fd = open(GROUP_PATH_MNT, O_RDONLY);
    if (fd < 0) fd = open(GROUP_PATH, O_RDONLY);
    if (fd < 0) return -1;
    int n = (int)read(fd, buf, (size_t)(bufsz - 1));
    close(fd);
    if (n <= 0) return -1;
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
    if (*p == ':') p++;
    *src = p;
    return i;
}

static int parse_members(const char* src, char** out, int maxmem)
{
    /* Copy src into s_membuf so we can point into it */
    int n = 0;
    char* w = s_membuf;
    const char* p = src;
    while (*p && *p != '\n' && n < maxmem) {
        const char* start = w;
        while (*p && *p != ',' && *p != '\n') {
            *w++ = *p++;
        }
        *w++ = '\0';
        if (start[0] != '\0') {
            out[n++] = (char*)start;
        }
        if (*p == ',') p++;
    }
    out[n] = NULL;
    return n;
}

/* Parse one line into s_grp. Returns 1 on success, 0 on skip. */
static int parse_line(const char* line)
{
    char gid_s[16];
    char members_raw[GROUP_BUF_MAX / 2];
    const char* p = line;

    if (!*p || *p == '#') return 0;

    next_field_s(&p, s_name,    sizeof(s_name));
    next_field_s(&p, s_passwd,  sizeof(s_passwd));
    next_field_s(&p, gid_s,     sizeof(gid_s));
    /* rest is member list (may be empty) */
    int mlen = 0;
    while (*p && *p != '\n' && mlen < (int)sizeof(members_raw) - 1) {
        members_raw[mlen++] = *p++;
    }
    members_raw[mlen] = '\0';

    uint32_t gid = 0;
    const char* gp = gid_s;
    while (*gp >= '0' && *gp <= '9') { gid = gid * 10u + (uint32_t)(*gp - '0'); gp++; }

    s_grp.gr_name   = s_name;
    s_grp.gr_passwd = s_passwd;
    s_grp.gr_gid    = (gid_t)gid;
    parse_members(members_raw, s_members, MEMBER_MAX);
    s_grp.gr_mem    = s_members;
    return 1;
}

struct group* getgrgid(gid_t gid)
{
    static char buf[GROUP_BUF_MAX];
    if (read_group_file(buf, sizeof(buf)) < 0) return NULL;

    const char* line = buf;
    while (*line) {
        const char* end = line;
        while (*end && *end != '\n') end++;

        char scratch[256];
        int llen = (int)(end - line);
        if (llen >= (int)sizeof(scratch)) llen = (int)sizeof(scratch) - 1;
        memcpy(scratch, line, (size_t)llen);
        scratch[llen] = '\0';

        if (parse_line(scratch) && s_grp.gr_gid == gid) {
            return &s_grp;
        }
        line = (*end == '\n') ? end + 1 : end;
    }
    return NULL;
}

struct group* getgrnam(const char* name)
{
    static char buf[GROUP_BUF_MAX];
    if (!name) return NULL;
    if (read_group_file(buf, sizeof(buf)) < 0) return NULL;

    const char* line = buf;
    while (*line) {
        const char* end = line;
        while (*end && *end != '\n') end++;

        char scratch[256];
        int llen = (int)(end - line);
        if (llen >= (int)sizeof(scratch)) llen = (int)sizeof(scratch) - 1;
        memcpy(scratch, line, (size_t)llen);
        scratch[llen] = '\0';

        if (parse_line(scratch) && strcmp(s_grp.gr_name, name) == 0) {
            return &s_grp;
        }
        line = (*end == '\n') ? end + 1 : end;
    }
    return NULL;
}
