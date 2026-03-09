#include <stdint.h>
#include <signal.h>
#include <unistd.h>

#define FD_STDOUT 1

static volatile int g_seen = 0;

static long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return write_buf(s, len);
}

static void on_usr1(int sig)
{
    if (sig == SIGTERM) {
        g_seen = 1;
    }
}

int main(void)
{
    struct sigaction sa;
    sa.sa_handler = on_usr1;
    sa.sa_flags = 0;
    sa.sa_restorer = (void (*)(void))0;
    sa.sa_mask = 0;
    if (sigaction(SIGTERM, &sa, (struct sigaction*)0) != 0) {
        (void)write_str("sigtest: sigaction failed\n");
        return 1;
    }
    if (kill(getpid(), SIGTERM) != 0) {
        (void)write_str("sigtest: kill failed\n");
        return 1;
    }
    if (!g_seen) {
        (void)write_str("sigtest: handler not observed\n");
        return 1;
    }
    (void)write_str("sigtest: PASS\n");
    return 0;
}
