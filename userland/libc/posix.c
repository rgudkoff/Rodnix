#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/*
 * signal() — thin wrapper around sigaction().
 * Returns previous handler or SIG_ERR on error.
 */
sighandler_t signal(int signum, sighandler_t handler)
{
    struct sigaction sa;
    struct sigaction old;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags   = 0;
    sa.sa_mask    = 0;

    if (sigaction(signum, &sa, &old) != 0) {
        return SIG_ERR;
    }
    return old.sa_handler;
}

/*
 * gettimeofday() — implemented via clock_gettime(CLOCK_REALTIME).
 * The timezone argument is ignored (POSIX allows this).
 */
int gettimeofday(struct timeval* tv, void* tz)
{
    (void)tz;
    if (!tv) {
        errno = EINVAL;
        return -1;
    }
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return -1;
    }
    tv->tv_sec  = ts.tv_sec;
    tv->tv_usec = (suseconds_t)(ts.tv_nsec / 1000);
    return 0;
}

/*
 * time() — seconds since epoch via clock_gettime.
 */
time_t time(time_t* tloc)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return (time_t)-1;
    }
    if (tloc) {
        *tloc = ts.tv_sec;
    }
    return ts.tv_sec;
}

/*
 * system() — execute a shell command.
 * Runs /bin/sh -c <cmd>; returns exit status or -1 on fork failure.
 * system(NULL) returns 1 (shell is always available).
 */
int system(const char* cmd)
{
    if (!cmd) {
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        /* child */
        char* const argv[] = { (char*)"/bin/sh", (char*)"-c", (char*)cmd, NULL };
        execve("/bin/sh", argv, environ);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return status;
}
