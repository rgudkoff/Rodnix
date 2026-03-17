#ifndef _RODNIX_USERLAND_GRP_H
#define _RODNIX_USERLAND_GRP_H

#include <sys/types.h>

#define GROUP_PATH     "/etc/group"
#define GROUP_PATH_MNT "/mnt/etc/group"

struct group {
    char*  gr_name;   /* group name */
    char*  gr_passwd; /* password field (usually "x") */
    gid_t  gr_gid;    /* group ID */
    char** gr_mem;    /* NULL-terminated member list */
};

struct group* getgrgid(gid_t gid);
struct group* getgrnam(const char* name);

#endif /* _RODNIX_USERLAND_GRP_H */
