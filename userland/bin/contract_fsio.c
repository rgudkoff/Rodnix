/*
 * contract_fsio.c
 * Contract checks for stat/fstat/lseek file-path semantics.
 *
 * Spec traceability:
 * - CT-021 -> docs/ru/unix_process_model.md (file descriptor path semantics)
 */

#include <stdint.h>
#include "posix_syscall.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/stat.h"

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
    int fd;
    struct stat st_path;
    struct stat st_fd;
    char a[8];
    char b[8];
    ssize_t n1;
    ssize_t n2;
    off_t end_pos;

    fd = open("/etc/hostname", O_RDONLY);
    if (fd < 0) {
        (void)write_str("[CT] CT-021 FAIL open('/etc/hostname') failed\n");
        return 1;
    }

    if (stat("/etc/hostname", &st_path) != 0) {
        (void)write_str("[CT] CT-021 FAIL stat('/etc/hostname') failed\n");
        ok = 0;
    }
    if (fstat(fd, &st_fd) != 0) {
        (void)write_str("[CT] CT-021 FAIL fstat(fd) failed\n");
        ok = 0;
    }
    if (ok && st_path.st_size != st_fd.st_size) {
        (void)write_str("[CT] CT-021 FAIL stat/fstat size mismatch\n");
        ok = 0;
    }

    n1 = read(fd, a, 4);
    if (n1 != 4) {
        (void)write_str("[CT] CT-021 FAIL first read size mismatch\n");
        ok = 0;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        (void)write_str("[CT] CT-021 FAIL lseek(fd,0,SEEK_SET) failed\n");
        ok = 0;
    }
    n2 = read(fd, b, 4);
    if (n2 != 4) {
        (void)write_str("[CT] CT-021 FAIL second read size mismatch\n");
        ok = 0;
    }
    if (n1 == 4 && n2 == 4) {
        for (int i = 0; i < 4; i++) {
            if (a[i] != b[i]) {
                (void)write_str("[CT] CT-021 FAIL lseek rewind content mismatch\n");
                ok = 0;
                break;
            }
        }
    }

    end_pos = lseek(fd, 0, SEEK_END);
    if (end_pos < 0) {
        (void)write_str("[CT] CT-021 FAIL lseek(fd,0,SEEK_END) failed\n");
        ok = 0;
    } else if ((off_t)st_fd.st_size != end_pos) {
        (void)write_str("[CT] CT-021 FAIL SEEK_END position mismatch\n");
        ok = 0;
    }

    if (close(fd) != 0) {
        (void)write_str("[CT] CT-021 FAIL close(fd) failed\n");
        ok = 0;
    }

    if (ok) {
        (void)write_str("[CT] CT-021 PASS stat/fstat/lseek contract\n");
        return 0;
    }
    return 1;
}
