/*
 * contract_exec_after.c
 * Second image for exec contract checks.
 *
 * Return status encoding for parent test:
 * - bit 16 set: image-specific state was reset after exec
 * - low 16 bits: observed pid in post-exec image
 */

#include <stdint.h>
#include "posix_syscall.h"

volatile int g_exec_probe_cookie = 0;

int main(void)
{
    long pid = posix_getpid();
    uint32_t status = ((uint32_t)pid) & 0xFFFFu;
    if (g_exec_probe_cookie == 0) {
        status |= 0x10000u;
    }
    return (int)status;
}
