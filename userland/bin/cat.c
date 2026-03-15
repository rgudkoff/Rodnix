/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Compact cat implementation reduced to raw mode for RodNIX libc-lite.
 */

#include <fcntl.h>
#include <unistd.h>

static int rval;
static const char* filename;

static unsigned long cstr_len(const char* s)
{
    unsigned long n = 0;
    if (!s) {
        return 0;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void put_str_fd(int fd, const char* s)
{
    (void)write(fd, s, cstr_len(s));
}

static void warn_open(const char* path)
{
    put_str_fd(STDERR_FILENO, "cat: ");
    put_str_fd(STDERR_FILENO, path ? path : "(null)");
    put_str_fd(STDERR_FILENO, ": open failed\n");
}

static void warn_read(const char* path)
{
    put_str_fd(STDERR_FILENO, "cat: ");
    put_str_fd(STDERR_FILENO, path ? path : "(null)");
    put_str_fd(STDERR_FILENO, ": read failed\n");
}

static void raw_cat(int fd)
{
    char buf[4096];
    for (;;) {
        long n = read(fd, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            warn_read(filename);
            rval = 1;
            break;
        }
        if (write(STDOUT_FILENO, buf, (unsigned long)n) < 0) {
            rval = 1;
            break;
        }
    }
}

static int is_dash(const char* s)
{
    return s && s[0] == '-' && s[1] == '\0';
}

static void scanfiles(int argc, char** argv)
{
    int fd = -1;
    if (argc <= 0) {
        filename = "stdin";
        raw_cat(STDIN_FILENO);
        return;
    }

    for (int i = 0; i < argc; i++) {
        const char* path = argv[i];
        if (path == 0 || is_dash(path)) {
            filename = "stdin";
            fd = STDIN_FILENO;
        } else {
            filename = path;
            fd = open(path, O_RDONLY);
            if (fd < 0) {
                warn_open(path);
                rval = 1;
                continue;
            }
        }

        raw_cat(fd);
        if (fd != STDIN_FILENO) {
            (void)close(fd);
        }
    }
}

int main(int argc, char** argv)
{
    rval = 0;
    scanfiles(argc - 1, argv + 1);
    return rval;
}
