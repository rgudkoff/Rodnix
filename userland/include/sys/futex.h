#ifndef _RODNIX_USERLAND_SYS_FUTEX_H
#define _RODNIX_USERLAND_SYS_FUTEX_H

#include <errno.h>
#include <sys/time.h>
#include "posix_syscall.h"

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static inline int futex(int* uaddr,
                        int op,
                        int val,
                        const struct timespec* timeout,
                        int* uaddr2,
                        int val3)
{
    long r = posix_futex(uaddr, op, val, timeout, uaddr2, val3);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

#endif /* _RODNIX_USERLAND_SYS_FUTEX_H */
