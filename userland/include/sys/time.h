#ifndef _RODNIX_USERLAND_SYS_TIME_H
#define _RODNIX_USERLAND_SYS_TIME_H

#include <sys/types.h>
#include <errno.h>
#include "posix_syscall.h"

typedef long suseconds_t;

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 4

typedef int clockid_t;

int gettimeofday(struct timeval* tv, void* tz);

static inline int settimeofday(const struct timeval* tv, const void* tz)
{
    long r = posix_settimeofday(tv, tz);
    if (r < 0) {
        errno = rdnx_errno_from_status(r);
        if (errno == EACCES) {
            errno = EPERM;
        }
        return -1;
    }
    return 0;
}

#endif /* _RODNIX_USERLAND_SYS_TIME_H */
