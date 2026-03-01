#ifndef _RODNIX_USERLAND_SYS_STAT_H
#define _RODNIX_USERLAND_SYS_STAT_H

#include <sys/types.h>

struct stat {
    mode_t st_mode;
    off_t st_size;
};

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000

#endif /* _RODNIX_USERLAND_SYS_STAT_H */
