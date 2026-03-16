#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define FD_STDOUT 1
#define FD_STDERR 2

static long write_buf(int fd, const char* s, uint64_t len)
{
    return posix_write(fd, s, len);
}

static long write_str(int fd, const char* s)
{
    uint64_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return write_buf(fd, s, len);
}

static int parse_u64(const char* s, uint64_t* out)
{
    uint64_t v = 0;
    if (!s || !out || s[0] == '\0') {
        return -1;
    }
    for (uint64_t i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        v = v * 10u + (uint64_t)(c - '0');
    }
    *out = v;
    return 0;
}

int main(int argc, char** argv)
{
    (void)argv;
    if (argc != 2 || !argv || !argv[1]) {
        (void)write_str(FD_STDERR, "usage: sleep <seconds>\n");
        return 1;
    }

    uint64_t sec = 0;
    if (parse_u64(argv[1], &sec) != 0) {
        (void)write_str(FD_STDERR, "sleep: invalid seconds\n");
        return 1;
    }

    struct timespec req;
    req.tv_sec = (time_t)sec;
    req.tv_nsec = 0;
    if (nanosleep(&req, (struct timespec*)0) != 0) {
        (void)write_str(FD_STDERR, "sleep: nanosleep failed\n");
        return 1;
    }
    return 0;
}
