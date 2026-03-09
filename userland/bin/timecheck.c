/*
 * timecheck.c
 * Validate CLOCK_MONOTONIC / CLOCK_REALTIME behavior.
 */

#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define FD_STDOUT 1
#define SYS_TEST_SLEEP 120

static long write_buf(const char* s, uint64_t len)
{
    return write(FD_STDOUT, s, (size_t)len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

static uint64_t ts_to_us(const struct timespec* ts)
{
    return ((uint64_t)ts->tv_sec * 1000000ULL) + ((uint64_t)ts->tv_nsec / 1000ULL);
}

int main(void)
{
    struct timespec m0, m1, r0, r1;

    if (clock_gettime(CLOCK_MONOTONIC, &m0) != 0 ||
        clock_gettime(CLOCK_REALTIME, &r0) != 0) {
        (void)write_str("timecheck: clock_gettime initial failed\n");
        return 1;
    }

    (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);

    if (clock_gettime(CLOCK_MONOTONIC, &m1) != 0 ||
        clock_gettime(CLOCK_REALTIME, &r1) != 0) {
        (void)write_str("timecheck: clock_gettime second failed\n");
        return 2;
    }

    if (ts_to_us(&m1) < ts_to_us(&m0)) {
        (void)write_str("timecheck: monotonic went backwards\n");
        return 3;
    }
    if (ts_to_us(&r1) < ts_to_us(&r0)) {
        (void)write_str("timecheck: realtime went backwards\n");
        return 4;
    }

    (void)write_str("timecheck: ok mono_us=");
    write_u64(ts_to_us(&m1) - ts_to_us(&m0));
    (void)write_str(" rt_us=");
    write_u64(ts_to_us(&r1) - ts_to_us(&r0));
    (void)write_str("\n");
    return 0;
}
