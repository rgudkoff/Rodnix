/*
 * contract_spawn_wait.c
 * Contract checks for spawn/wait semantics.
 *
 * Spec traceability:
 * - CT-001 -> docs/ru/unix_process_model.md (spawn creates new child PID)
 * - CT-005 -> docs/ru/unix_process_model.md (exit/wait lifecycle semantics)
 *
 * Note:
 * - CT-005 is currently PLANNED in CI gating and this binary is kept for
 *   manual/additive runs until waitpid lifecycle criteria are satisfied.
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

int main(void)
{
    int ok = 1;
    int status = -1;

    long pid = posix_spawn("/bin/true", 0);
    if (pid > 0) {
        (void)write_str("[CT] CT-001 PASS spawn creates child pid\n");
    } else {
        (void)write_str("[CT] CT-001 FAIL spawn failed\n");
        return 1;
    }

    {
        long wr = waitpid((pid_t)pid, &status, 0);
        if (wr == pid && status == 0) {
            (void)write_str("[CT] CT-005 PASS waitpid reaps child exit status\n");
        } else {
            (void)write_str("[CT] CT-005 FAIL waitpid failed or bad status\n");
            ok = 0;
        }
    }

    return ok ? 0 : 1;
}
