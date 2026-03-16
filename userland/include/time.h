#ifndef _RODNIX_USERLAND_TIME_H
#define _RODNIX_USERLAND_TIME_H

#include <sys/time.h>
#include <errno.h>
#include "posix_syscall.h"

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t time(time_t* tloc);
struct tm* localtime(const time_t* timer);

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
