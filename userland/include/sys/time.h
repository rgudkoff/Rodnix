#ifndef _RODNIX_USERLAND_SYS_TIME_H
#define _RODNIX_USERLAND_SYS_TIME_H

#include <sys/types.h>

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

#endif /* _RODNIX_USERLAND_SYS_TIME_H */
