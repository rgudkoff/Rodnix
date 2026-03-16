#ifndef _RODNIX_USERLAND_SYS_SELECT_H
#define _RODNIX_USERLAND_SYS_SELECT_H

#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include "posix_syscall.h"

#define FD_SETSIZE 32

typedef struct {
    uint32_t fds_bits;
} fd_set;

#define FD_ZERO(setp) do { (setp)->fds_bits = 0u; } while (0)
#define FD_SET(fd, setp) do { if ((fd) >= 0 && (fd) < FD_SETSIZE) { (setp)->fds_bits |= (1u << (fd)); } } while (0)
#define FD_CLR(fd, setp) do { if ((fd) >= 0 && (fd) < FD_SETSIZE) { (setp)->fds_bits &= ~(1u << (fd)); } } while (0)
#define FD_ISSET(fd, setp) (((fd) >= 0 && (fd) < FD_SETSIZE) ? (((setp)->fds_bits & (1u << (fd))) != 0u) : 0)

static inline int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout)
{
    long r = posix_select(nfds, readfds, writefds, exceptfds, timeout);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

#endif /* _RODNIX_USERLAND_SYS_SELECT_H */
