#ifndef _RODNIX_USERLAND_SYS_DIRENT_H
#define _RODNIX_USERLAND_SYS_DIRENT_H

#include <sys/types.h>

/* FreeBSD-compatible d_type values */
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

struct dirent {
    uint64_t d_fileno;
    uint16_t d_reclen;
    uint8_t d_type;
    uint8_t d_namlen;
    char d_name[NAME_MAX + 1];
};

#endif /* _RODNIX_USERLAND_SYS_DIRENT_H */
