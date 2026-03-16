#ifndef _RODNIX_USERLAND_SYS_STAT_H
#define _RODNIX_USERLAND_SYS_STAT_H

#include <sys/types.h>
#include <errno.h>
#include "posix_syscall.h"

struct stat {
    mode_t st_mode;
    off_t st_size;
    time_t st_mtime;
};

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  040000
#define S_IFLNK  0120000
#define S_IFBLK  060000
#define S_IFCHR  020000
#define S_IFIFO  010000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

#define S_ISUID  04000
#define S_ISGID  02000
#define S_ISVTX  01000

#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRWXU  0700

#define S_IRGRP  040
#define S_IWGRP  020
#define S_IXGRP  010
#define S_IRWXG  070

#define S_IROTH  04
#define S_IWOTH  02
#define S_IXOTH  01
#define S_IRWXO  07

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

static inline int lstat(const char* path, struct stat* st)
{
    long r = posix_lstat(path, st);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int chmod(const char* path, mode_t mode)
{
    long r = posix_chmod(path, (unsigned int)mode);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int fchmod(int fd, mode_t mode)
{
    long r = posix_fchmod(fd, (unsigned int)mode);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

#endif /* _RODNIX_USERLAND_SYS_STAT_H */
