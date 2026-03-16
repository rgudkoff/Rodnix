#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "posix_syscall.h"

DIR* opendir(const char* path)
{
    DIR* d;
    struct dirent* ents = 0;
    size_t cap_count = 64;
    size_t cap_bytes = cap_count * sizeof(struct dirent);
    long n;
    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return 0;
    }

    d = (DIR*)malloc(sizeof(*d));
    if (!d) {
        errno = ENOMEM;
        return 0;
    }
    memset(d, 0, sizeof(*d));
    strncpy(d->path, path, sizeof(d->path) - 1);

    ents = (struct dirent*)malloc(cap_bytes);
    if (!ents) {
        free(d);
        errno = ENOMEM;
        return 0;
    }

    n = posix_readdir(d->path, ents, cap_bytes);
    if (n < 0) {
        free(ents);
        free(d);
        errno = (int)(-n);
        return 0;
    }

    d->entries = ents;
    d->count = (size_t)n / sizeof(struct dirent);
    d->index = 0;
    return d;
}

struct dirent* readdir(DIR* dirp)
{
    if (!dirp || !dirp->entries) {
        errno = EBADF;
        return 0;
    }
    while (dirp->index < dirp->count) {
        struct dirent* out = &dirp->entries[dirp->index++];
        if (out->d_name[0] != '\0') {
            return out;
        }
    }
    return 0;
}

int closedir(DIR* dirp)
{
    if (!dirp) {
        errno = EBADF;
        return -1;
    }
    free(dirp->entries);
    free(dirp);
    return 0;
}
