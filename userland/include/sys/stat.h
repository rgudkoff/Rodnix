#ifndef _RODNIX_USERLAND_SYS_STAT_H
#define _RODNIX_USERLAND_SYS_STAT_H

#include <sys/types.h>
#include <errno.h>
#include "posix_syscall.h"

struct stat {
    mode_t st_mode;
    off_t st_size;
};

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  040000
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100

static inline int stat(const char* path, struct stat* st)
{
    long r = posix_stat(path, st);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int fstat(int fd, struct stat* st)
{
    long r = posix_fstat(fd, st);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

#endif /* _RODNIX_USERLAND_SYS_STAT_H */
