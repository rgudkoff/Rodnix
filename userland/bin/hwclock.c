/*
 * hwclock - read or synchronize the hardware RTC clock.
 *
 * Usage:
 *   hwclock
 *   hwclock -r
 *   hwclock -s   # set system time from RTC
 *   hwclock -w   # write current system time to RTC
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "posix_syscall.h"

static void usage(void)
{
    fprintf(stderr, "usage: hwclock [-r|-s|-w] [-u]\n");
}

static int rtc_gettimeofday(struct timeval* tv)
{
    long r = posix_rtc_gettime(tv);
    if (r < 0) {
        errno = rdnx_errno_from_status(r);
        return -1;
    }
    return 0;
}

static int rtc_settimeofday(const struct timeval* tv)
{
    long r = posix_rtc_settime(tv);
    if (r < 0) {
        errno = rdnx_errno_from_status(r);
        if (errno == EACCES) {
            errno = EPERM;
        }
        return -1;
    }
    return 0;
}

static int print_hwclock(int use_utc)
{
    struct timeval tv;
    struct tm tm;
    char out[128];

    if (rtc_gettimeofday(&tv) != 0) {
        perror("hwclock");
        return 1;
    }
    if (use_utc) {
        if (!gmtime_r(&tv.tv_sec, &tm)) {
            perror("hwclock");
            return 1;
        }
    } else {
        if (!localtime_r(&tv.tv_sec, &tm)) {
            perror("hwclock");
            return 1;
        }
    }
    if (strftime(out, sizeof(out), "%a %b %d %T %z %Y", &tm) == 0) {
        fprintf(stderr, "hwclock: format failed\n");
        return 1;
    }
    puts(out);
    return 0;
}

int main(int argc, char** argv)
{
    enum {
        MODE_READ = 0,
        MODE_HCTOSYS,
        MODE_SYSTOHC
    } mode = MODE_READ;
    int use_utc = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            mode = MODE_READ;
        } else if (strcmp(argv[i], "-s") == 0) {
            mode = MODE_HCTOSYS;
        } else if (strcmp(argv[i], "-w") == 0) {
            mode = MODE_SYSTOHC;
        } else if (strcmp(argv[i], "-u") == 0) {
            use_utc = 1;
        } else {
            usage();
            return 1;
        }
    }

    if (mode == MODE_READ) {
        return print_hwclock(use_utc);
    }

    if (mode == MODE_HCTOSYS) {
        struct timeval tv;
        if (rtc_gettimeofday(&tv) != 0) {
            perror("hwclock");
            return 1;
        }
        if (settimeofday(&tv, NULL) != 0) {
            perror("hwclock");
            return 1;
        }
        return 0;
    }

    {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) != 0) {
            perror("hwclock");
            return 1;
        }
        if (rtc_settimeofday(&tv) != 0) {
            perror("hwclock");
            return 1;
        }
    }
    return 0;
}
