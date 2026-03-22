#include <errno.h>
#include <signal.h>
#include <sys/signal.h>
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

static int parse_tz_offset_seconds(void)
{
    const char* tz = getenv("TZ");
    int sign = 1;
    int hours = 0;
    int mins = 0;

    if (!tz || !*tz) {
        return 0;
    }
    if (strcmp(tz, "UTC") == 0 || strcmp(tz, "GMT") == 0 || strcmp(tz, "Z") == 0) {
        return 0;
    }
    if (strncmp(tz, "UTC", 3) == 0 || strncmp(tz, "GMT", 3) == 0) {
        tz += 3;
    }
    if (*tz == '\0') {
        return 0;
    }
    if (*tz == '+') {
        sign = 1;
        tz++;
    } else if (*tz == '-') {
        sign = -1;
        tz++;
    } else {
        return 0;
    }
    if (tz[0] < '0' || tz[0] > '9' || tz[1] < '0' || tz[1] > '9') {
        return 0;
    }
    hours = (tz[0] - '0') * 10 + (tz[1] - '0');
    tz += 2;
    if (*tz == ':') {
        tz++;
    }
    if (*tz != '\0') {
        if (tz[0] < '0' || tz[0] > '9' || tz[1] < '0' || tz[1] > '9') {
            return 0;
        }
        mins = (tz[0] - '0') * 10 + (tz[1] - '0');
        tz += 2;
    }
    if (*tz != '\0' || hours > 23 || mins > 59) {
        return 0;
    }
    return sign * (hours * 3600 + mins * 60);
}

static struct tm* break_time_utc(const time_t* timer, struct tm* out)
{
    static const int days_before_month[2][12] = {
        { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 },
        { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 }
    };
    time_t secs;
    long days;
    int year;
    int leap;
    int month;

    if (!timer || !out) {
        errno = EINVAL;
        return NULL;
    }

    secs = *timer;
    if (secs < 0) {
        errno = EINVAL;
        return NULL;
    }

    memset(out, 0, sizeof(*out));
    out->tm_sec = (int)(secs % 60);
    secs /= 60;
    out->tm_min = (int)(secs % 60);
    secs /= 60;
    out->tm_hour = (int)(secs % 24);
    days = (long)(secs / 24);

    out->tm_wday = (int)((days + 4) % 7);

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
    out->tm_year = year - 1900;
    out->tm_yday = (int)days;

    for (month = 11; month > 0; --month) {
        if (days >= days_before_month[leap][month]) {
            break;
        }
    }

    out->tm_mon = month;
    out->tm_mday = (int)(days - days_before_month[leap][month]) + 1;
    out->tm_isdst = 0;
    return out;
}

struct tm* localtime_r(const time_t* timer, struct tm* result)
{
    time_t shifted;

    if (!timer || !result) {
        errno = EINVAL;
        return NULL;
    }
    shifted = *timer + (time_t)parse_tz_offset_seconds();
    return break_time_utc(&shifted, result);
}

struct tm* gmtime_r(const time_t* timer, struct tm* result)
{
    return break_time_utc(timer, result);
}

struct tm* localtime(const time_t* timer)
{
    static struct tm tm;
    return localtime_r(timer, &tm);
}

struct tm* gmtime(const time_t* timer)
{
    static struct tm tm;
    return gmtime_r(timer, &tm);
}

static size_t append_str(char* dst, size_t max, size_t pos, const char* src)
{
    while (src && *src) {
        if (pos + 1 >= max) {
            return max;
        }
        dst[pos++] = *src++;
    }
    return pos;
}

static size_t append_num(char* dst, size_t max, size_t pos, int value, int width)
{
    char buf[16];
    int idx = width;
    if (idx > (int)sizeof(buf) - 1) {
        idx = (int)sizeof(buf) - 1;
    }
    buf[idx] = '\0';
    while (idx > 0) {
        buf[--idx] = (char)('0' + (value % 10));
        value /= 10;
    }
    return append_str(dst, max, pos, buf);
}

size_t strftime(char* s, size_t max, const char* format, const struct tm* tm)
{
    static const char* wday_names[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char* month_names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    size_t pos = 0;

    if (!s || !format || !tm || max == 0) {
        errno = EINVAL;
        return 0;
    }

    while (*format) {
        if (*format != '%') {
            if (pos + 1 >= max) {
                s[0] = '\0';
                return 0;
            }
            s[pos++] = *format++;
            continue;
        }

        format++;
        if (*format == '\0') {
            break;
        }

        switch (*format) {
        case '%':
            if (pos + 1 >= max) {
                s[0] = '\0';
                return 0;
            }
            s[pos++] = '%';
            break;
        case 'Y':
            pos = append_num(s, max, pos, tm->tm_year + 1900, 4);
            break;
        case 'm':
            pos = append_num(s, max, pos, tm->tm_mon + 1, 2);
            break;
        case 'd':
            pos = append_num(s, max, pos, tm->tm_mday, 2);
            break;
        case 'H':
            pos = append_num(s, max, pos, tm->tm_hour, 2);
            break;
        case 'M':
            pos = append_num(s, max, pos, tm->tm_min, 2);
            break;
        case 'S':
            pos = append_num(s, max, pos, tm->tm_sec, 2);
            break;
        case 'a':
            pos = append_str(s, max, pos,
                             (tm->tm_wday >= 0 && tm->tm_wday < 7) ? wday_names[tm->tm_wday] : "???");
            break;
        case 'b':
            pos = append_str(s, max, pos,
                             (tm->tm_mon >= 0 && tm->tm_mon < 12) ? month_names[tm->tm_mon] : "???");
            break;
        case 'F':
            pos = append_num(s, max, pos, tm->tm_year + 1900, 4);
            pos = append_str(s, max, pos, "-");
            pos = append_num(s, max, pos, tm->tm_mon + 1, 2);
            pos = append_str(s, max, pos, "-");
            pos = append_num(s, max, pos, tm->tm_mday, 2);
            break;
        case 'T':
            pos = append_num(s, max, pos, tm->tm_hour, 2);
            pos = append_str(s, max, pos, ":");
            pos = append_num(s, max, pos, tm->tm_min, 2);
            pos = append_str(s, max, pos, ":");
            pos = append_num(s, max, pos, tm->tm_sec, 2);
            break;
        case 'z':
        {
            int offset = parse_tz_offset_seconds();
            int abs_offset = (offset < 0) ? -offset : offset;
            if (pos + 5 >= max) {
                s[0] = '\0';
                return 0;
            }
            s[pos++] = (offset < 0) ? '-' : '+';
            pos = append_num(s, max, pos, abs_offset / 3600, 2);
            pos = append_num(s, max, pos, (abs_offset % 3600) / 60, 2);
            break;
        }
        default:
            if (pos + 2 >= max) {
                s[0] = '\0';
                return 0;
            }
            s[pos++] = '%';
            s[pos++] = *format;
            break;
        }

        if (pos >= max) {
            s[0] = '\0';
            return 0;
        }
        format++;
    }

    s[pos] = '\0';
    return pos;
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
