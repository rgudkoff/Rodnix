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

static int is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

struct tm* localtime(const time_t* timer)
{
    static struct tm tm;
    static const int days_before_month[2][12] = {
        { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 },
        { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 }
    };
    time_t secs;
    long days;
    int year;
    int leap;
    int month;

    if (!timer) {
        errno = EINVAL;
        return NULL;
    }

    secs = *timer;
    if (secs < 0) {
        errno = EINVAL;
        return NULL;
    }

    memset(&tm, 0, sizeof(tm));
    tm.tm_sec = (int)(secs % 60);
    secs /= 60;
    tm.tm_min = (int)(secs % 60);
    secs /= 60;
    tm.tm_hour = (int)(secs % 24);
    days = (long)(secs / 24);

    tm.tm_wday = (int)((days + 4) % 7);

    year = 1970;
    while (1) {
        int days_in_year = is_leap_year(year) ? 366 : 365;
        if (days < days_in_year) {
            break;
        }
        days -= days_in_year;
        ++year;
    }

    leap = is_leap_year(year);
    tm.tm_year = year - 1900;
    tm.tm_yday = (int)days;

    for (month = 11; month > 0; --month) {
        if (days >= days_before_month[leap][month]) {
            break;
        }
    }

    tm.tm_mon = month;
    tm.tm_mday = (int)(days - days_before_month[leap][month]) + 1;
    tm.tm_isdst = 0;
    return &tm;
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

int execvp(const char* file, char* const argv[])
{
    const char* path_env;
    const char* start;
    const char* end;

    if (!file || !*file) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/')) {
        return execve(file, argv, environ);
    }

    path_env = getenv("PATH");
    if (!path_env || !*path_env) {
        path_env = "/bin:/usr/bin";
    }

    start = path_env;
    while (*start) {
        char candidate[256];
        size_t dir_len;

        end = start;
        while (*end && *end != ':') {
            end++;
        }
        dir_len = (size_t)(end - start);
        if (dir_len + 1 + strlen(file) + 1 < sizeof(candidate)) {
            memcpy(candidate, start, dir_len);
            candidate[dir_len] = '/';
            strcpy(candidate + dir_len + 1, file);
            execve(candidate, argv, environ);
            if (errno != ENOENT) {
                return -1;
            }
        }
        start = (*end == ':') ? end + 1 : end;
    }

    errno = ENOENT;
    return -1;
}
