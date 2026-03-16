#ifndef _RODNIX_USERLAND_SIGNAL_H
#define _RODNIX_USERLAND_SIGNAL_H

#include <sys/signal.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include "posix_syscall.h"

static inline void __attribute__((noreturn)) __rdnx_sigreturn_restorer(void)
{
    (void)posix_sigreturn();
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static inline int kill(pid_t pid, int sig)
{
    long r = posix_kill((long)pid, sig);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
    struct sigaction local;
    const struct sigaction* send = act;
    if (act) {
        local = *act;
        if (local.sa_handler != SIG_DFL &&
            local.sa_handler != SIG_IGN &&
            local.sa_restorer == (void (*)(void))0) {
            local.sa_restorer = __rdnx_sigreturn_restorer;
        }
        send = &local;
    }
    long r = posix_sigaction(signum, send, oldact);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return 0;
}

static inline int sigreturn(void)
{
    long r = posix_sigreturn();
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

#endif /* _RODNIX_USERLAND_SIGNAL_H */
