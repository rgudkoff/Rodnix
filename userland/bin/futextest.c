#include <stdint.h>
#include <unistd.h>
#include <sys/futex.h>

#define FD_STDOUT 1

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

int main(void)
{
    int f = 1;
    struct timespec ts;

    if (futex(&f, FUTEX_WAIT, 0, (const struct timespec*)0, (int*)0, 0) >= 0) {
        (void)write_str("futextest: wait mismatch should fail\n");
        return 1;
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 1000000;
    if (futex(&f, FUTEX_WAIT, 1, &ts, (int*)0, 0) >= 0) {
        (void)write_str("futextest: timed wait should fail\n");
        return 1;
    }

    if (futex(&f, FUTEX_WAKE, 1, (const struct timespec*)0, (int*)0, 0) != 0) {
        (void)write_str("futextest: wake expected 0\n");
        return 1;
    }

    (void)write_str("futextest: PASS\n");
    return 0;
}
