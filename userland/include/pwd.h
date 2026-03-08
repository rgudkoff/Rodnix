#ifndef _RODNIX_USERLAND_PWD_H
#define _RODNIX_USERLAND_PWD_H

#include <sys/types.h>

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

#endif /* _RODNIX_USERLAND_PWD_H */
