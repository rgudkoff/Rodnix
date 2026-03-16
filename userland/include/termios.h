#ifndef _RODNIX_USERLAND_TERMIOS_H
#define _RODNIX_USERLAND_TERMIOS_H

#include <sys/termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "posix_syscall.h"

static inline int tcgetattr(int fd, struct termios* t)
{
    long r = posix_ioctl(fd, RDNX_TTY_IOCTL_GETATTR, t);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int tcsetattr(int fd, int optional_actions, const struct termios* t)
{
    if (optional_actions != TCSANOW) {
        errno = EINVAL;
        return -1;
    }
    long r = posix_ioctl(fd, RDNX_TTY_IOCTL_SETATTR, (void*)(uintptr_t)t);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

#endif /* _RODNIX_USERLAND_TERMIOS_H */
