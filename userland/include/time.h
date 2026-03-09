#ifndef _RODNIX_USERLAND_TIME_H
#define _RODNIX_USERLAND_TIME_H

#include <sys/time.h>
#include <errno.h>
#include "posix_syscall.h"

static inline int clock_gettime(clockid_t clk_id, struct timespec* tp)
{
    long r = posix_clock_gettime((int)clk_id, tp);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int nanosleep(const struct timespec* req, struct timespec* rem)
{
    long r = posix_nanosleep(req, rem);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

#endif /* _RODNIX_USERLAND_TIME_H */
