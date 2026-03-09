#ifndef _RODNIX_USERLAND_SYS_POLL_H
#define _RODNIX_USERLAND_SYS_POLL_H

#include <sys/types.h>
#include <errno.h>
#include "posix_syscall.h"

struct pollfd {
    int fd;
    short events;
    short revents;
};

#define POLLIN   0x0001
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

static inline int poll(struct pollfd* fds, nfds_t nfds, int timeout)
{
    long r = posix_poll(fds, (uint64_t)nfds, timeout);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

#endif /* _RODNIX_USERLAND_SYS_POLL_H */
