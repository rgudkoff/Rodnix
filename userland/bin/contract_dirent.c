/*
 * contract_dirent.c
 * Contract checks for handle-based dirent API.
 *
 * Spec traceability:
 * - CT-020 -> docs/ru/unix_process_model.md (opendir/readdir/closedir lifecycle)
 */

#include <stdint.h>
#include "posix_syscall.h"
#include "dirent.h"
#include "errno.h"

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
    int entries = 0;
    DIR* d;
    struct dirent* de;

    errno = 0;
    d = opendir("/etc");
    if (!d) {
        (void)write_str("[CT] CT-020 FAIL opendir('/etc') failed\n");
        return 1;
    }

    while ((de = readdir(d)) != 0) {
        if (de->d_name[0] == '\0') {
            continue;
        }
        entries++;
    }

    if (entries > 0) {
        (void)write_str("[CT] CT-020 PASS readdir('/etc') returned entries\n");
    } else {
        (void)write_str("[CT] CT-020 FAIL readdir('/etc') returned no entries\n");
        ok = 0;
    }

    if (closedir(d) != 0) {
        (void)write_str("[CT] CT-020 FAIL closedir('/etc') failed\n");
        ok = 0;
    }

    errno = 0;
    if (opendir("/no/such/path") == 0 && errno != 0) {
        (void)write_str("[CT] CT-020 PASS opendir missing path fails with errno\n");
    } else {
        (void)write_str("[CT] CT-020 FAIL opendir missing path behavior mismatch\n");
        ok = 0;
    }

    errno = 0;
    if (readdir(0) == 0 && errno == EBADF) {
        (void)write_str("[CT] CT-020 PASS readdir(NULL) sets EBADF\n");
    } else {
        (void)write_str("[CT] CT-020 FAIL readdir(NULL) behavior mismatch\n");
        ok = 0;
    }

    return ok ? 0 : 1;
}
