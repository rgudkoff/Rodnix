/*
 * contract_wait_nonchild.c
 * Contract check for waitpid(non-child) denial semantics.
 *
 * Spec traceability:
 * - CT-004 -> docs/ru/unix_process_model.md (waitpid only for child process)
 */

#include <stdint.h>
#include "posix_syscall.h"
#include "unistd.h"

#define FD_STDOUT 1

static long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

static int parse_u64(const char* s, uint64_t* out)
{
    uint64_t v = 0;
    int i = 0;
    if (!s || !out || s[0] == '\0') {
        return -1;
    }
    while (s[i] != '\0') {
        char c = s[i++];
        if (c < '0' || c > '9') {
            return -1;
        }
        v = v * 10u + (uint64_t)(c - '0');
    }
    *out = v;
    return 0;
}

static void u64_to_dec(uint64_t v, char* out, int out_len)
{
    char tmp[32];
    int i = 0;
    int j = 0;
    if (!out || out_len <= 1) {
        return;
    }
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0 && j + 1 < out_len) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

int main(int argc, char** argv)
{
    enum { RDNX_E_DENIED = -6 };
    int status = -1;

    if (argc >= 2 && argv[1]) {
        uint64_t target = 0;
        if (parse_u64(argv[1], &target) != 0 || target == 0) {
            return 3;
        }
        {
            long wr = posix_waitpid((long)target, &status);
            if (wr == (long)RDNX_E_DENIED) {
                return 0;
            }
            return 2;
        }
    }

    {
        long pid_a;
        long pid_b;
        long wr;
        char pid_buf[24];
        const char* av_b[3];

        pid_a = posix_spawn("/bin/true", 0);
        if (pid_a <= 0) {
            (void)write_str("[CT] CT-004 FAIL orchestrator spawn A failed\n");
            return 1;
        }

        u64_to_dec((uint64_t)pid_a, pid_buf, (int)sizeof(pid_buf));
        av_b[0] = "/bin/contract_wait_nonchild";
        av_b[1] = pid_buf;
        av_b[2] = 0;
        pid_b = posix_spawn("/bin/contract_wait_nonchild", av_b);
        if (pid_b <= 0) {
            (void)write_str("[CT] CT-004 FAIL orchestrator spawn B failed\n");
            return 1;
        }

        wr = waitpid((pid_t)pid_b, &status, 0);
        if (wr != pid_b || status != 0) {
            (void)write_str("[CT] CT-004 FAIL waitpid(non-child) not denied\n");
            (void)waitpid((pid_t)pid_a, &status, 0);
            return 1;
        }

        wr = waitpid((pid_t)pid_a, &status, 0);
        if (wr != pid_a) {
            (void)write_str("[CT] CT-004 FAIL parent could not reap child\n");
            return 1;
        }

        (void)write_str("[CT] CT-004 PASS waitpid(non-child) denied\n");
        return 0;
    }
}
