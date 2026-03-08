#ifndef _RODNIX_USERLAND_DIRENT_H
#define _RODNIX_USERLAND_DIRENT_H

#include <stddef.h>
#include <sys/dirent.h>

typedef struct {
    int fd;
    char path[256];
    struct dirent* entries;
    size_t count;
    size_t index;
} DIR;

DIR* opendir(const char* path);
struct dirent* readdir(DIR* dirp);
int closedir(DIR* dirp);

#endif /* _RODNIX_USERLAND_DIRENT_H */
