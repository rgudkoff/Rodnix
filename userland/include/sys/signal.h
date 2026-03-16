#ifndef _RODNIX_USERLAND_SYS_SIGNAL_H
#define _RODNIX_USERLAND_SYS_SIGNAL_H

typedef void (*sighandler_t)(int);

#define SIG2STR_MAX 32

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGABRT   6
#define SIGKILL   9
#define SIGALRM   14
#define SIGTERM   15

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

struct sigaction {
    sighandler_t sa_handler;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    unsigned long sa_mask;
};

#endif /* _RODNIX_USERLAND_SYS_SIGNAL_H */
