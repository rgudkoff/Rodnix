#ifndef _RODNIX_USERLAND_PWD_H
#define _RODNIX_USERLAND_PWD_H

#include <sys/types.h>

#define PASSWD_PATH     "/etc/passwd"
#define PASSWD_PATH_MNT "/mnt/etc/passwd"

#define _PASSWORD_LEN 128

struct passwd {
    char* pw_name;
    char* pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    time_t pw_change;
    char* pw_class;
    char* pw_gecos;
    char* pw_dir;
    char* pw_shell;
    time_t pw_expire;
};

struct passwd* getpwuid(uid_t uid);
struct passwd* getpwnam(const char* name);

#endif /* _RODNIX_USERLAND_PWD_H */
