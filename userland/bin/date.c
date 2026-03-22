/*
 * date — print current date/time.
 * Usage:
 *   date
 *   date -u
 *   date -s VALUE
 *   date +FORMAT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static void usage(void)
{
    fprintf(stderr, "usage: date [-u] [-s VALUE] [+FORMAT]\n");
}

static int is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static long long days_before_year(int year)
{
    long long days = 0;
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    return days;
}

static long long make_utc_epoch_seconds(int year, int month, int day, int hour, int min, int sec)
{
    static const int mdays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    long long days = 0;

    if (year < 1970 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 ||
        min < 0 || min > 59 || sec < 0 || sec > 59) {
        return -1;
    }

    days = days_before_year(year);
    for (int m = 1; m < month; m++) {
        days += mdays[m - 1];
        if (m == 2 && is_leap_year(year)) {
            days += 1;
        }
    }
    if (day > mdays[month - 1] + ((month == 2 && is_leap_year(year)) ? 1 : 0)) {
        return -1;
    }
    days += day - 1;
    return days * 86400LL + hour * 3600LL + min * 60LL + sec;
}

static int parse_tz_offset_seconds(void)
{
    const char* tz = getenv("TZ");
    int sign = 1;
    int hours = 0;
    int mins = 0;

    if (!tz || !*tz || strcmp(tz, "UTC") == 0 || strcmp(tz, "GMT") == 0 || strcmp(tz, "Z") == 0) {
        return 0;
    }
    if (strncmp(tz, "UTC", 3) == 0 || strncmp(tz, "GMT", 3) == 0) {
        tz += 3;
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

static int parse_set_value(const char* value, int use_utc, struct timeval* tv)
{
    long long epoch = -1;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int min = 0;
    int sec = 0;

    if (!value || !tv) {
        return -1;
    }
    if (value[0] == '@') {
        char* end = NULL;
        epoch = (long long)strtol(value + 1, &end, 10);
        if (!end || *end != '\0' || epoch < 0) {
            return -1;
        }
    } else if (strlen(value) == 19 && value[4] == '-' && value[7] == '-' &&
               (value[10] == 'T' || value[10] == ' ') && value[13] == ':' && value[16] == ':' &&
               sscanf(value, "%d-%d-%d%*c%d:%d:%d", &year, &month, &day, &hour, &min, &sec) == 6) {
        epoch = make_utc_epoch_seconds(year, month, day, hour, min, sec);
    } else if (strlen(value) == 10 && value[4] == '-' && value[7] == '-' &&
               sscanf(value, "%d-%d-%d", &year, &month, &day) == 3) {
        epoch = make_utc_epoch_seconds(year, month, day, 0, 0, 0);
    }

    if (epoch < 0) {
        return -1;
    }
    if (!use_utc) {
        epoch -= parse_tz_offset_seconds();
    }
    if (epoch < 0) {
        return -1;
    }
    tv->tv_sec = (time_t)epoch;
    tv->tv_usec = 0;
    return 0;
}

int main(int argc, char** argv)
{
    const char* format = "%a %b %d %T %z %Y";
    const char* set_value = NULL;
    int use_utc = 0;
    time_t now;
    struct tm tm;
    char out[128];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0) {
            use_utc = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            set_value = argv[++i];
        } else if (argv[i][0] == '+') {
            format = argv[i] + 1;
        } else {
            usage();
            return 1;
        }
    }

    if (set_value) {
        struct timeval tv;
        if (parse_set_value(set_value, use_utc, &tv) != 0) {
            fprintf(stderr, "date: invalid time '%s'\n", set_value);
            return 1;
        }
        if (settimeofday(&tv, NULL) != 0) {
            perror("date");
            return 1;
        }
    }

    now = time(NULL);
    if (now == (time_t)-1) {
        perror("date");
        return 1;
    }

    if (use_utc) {
        if (!gmtime_r(&now, &tm)) {
            perror("date");
            return 1;
        }
    } else {
        if (!localtime_r(&now, &tm)) {
            perror("date");
            return 1;
        }
    }

    if (strftime(out, sizeof(out), format, &tm) == 0) {
        fprintf(stderr, "date: format too long or invalid\n");
        return 1;
    }

    puts(out);
    return 0;
}
