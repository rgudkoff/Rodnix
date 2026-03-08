/*
 * contract_exec_probe.c
 * Contract helper for exec semantics.
 *
 * Spec traceability:
 * - CT-003 -> exec preserves process identity (checked by parent via wait status)
 * - CT-011 -> exec resets image-specific state (encoded by exec-after image)
 */

#include "posix_syscall.h"

volatile int g_exec_probe_cookie = 0;

int main(void)
{
    g_exec_probe_cookie = 0x7A11;
    if (posix_exec("/bin/contract_exec_after") < 0) {
        return 0;
    }
    return 0;
}
