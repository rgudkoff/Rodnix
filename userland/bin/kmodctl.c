/*
 * kmodctl.c
 * Minimal userland frontend for kernel module registry.
 */

#include <stdint.h>
#include "posix_syscall.h"
#include "kmodinfo.h"

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

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

static int streq(const char* a, const char* b)
{
    uint64_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static void usage(void)
{
    (void)write_str("usage:\n");
    (void)write_str("  kmodctl ls\n");
    (void)write_str("  kmodctl load <path>\n");
    (void)write_str("  kmodctl unload <name>\n");
}

int main(int argc, char** argv)
{
    if (argc < 2 || !argv[1]) {
        usage();
        return 1;
    }

    if (streq(argv[1], "ls")) {
        rodnix_kmod_info_t mods[32];
        uint32_t total = 0;
        long n = posix_kmodls(mods, 32, &total);
        if (n < 0) {
            (void)write_str("kmodctl: kmodls failed\n");
            return 1;
        }
        (void)write_str("kmods: ");
        write_u64((uint64_t)n);
        (void)write_str("/");
        write_u64((uint64_t)total);
        (void)write_str("\n");
        for (uint32_t i = 0; i < (uint32_t)n; i++) {
            (void)write_str("  ");
            (void)write_str(mods[i].name);
            (void)write_str(" type=");
            (void)write_str(mods[i].kind);
            (void)write_str(" ver=");
            (void)write_str(mods[i].version);
            (void)write_str(" ");
            (void)write_str(mods[i].builtin ? "builtin" : "loadable");
            (void)write_str(" ");
            (void)write_str(mods[i].loaded ? "loaded" : "unloaded");
            (void)write_str("\n");
        }
        return 0;
    }

    if (streq(argv[1], "load")) {
        if (argc < 3 || !argv[2]) {
            usage();
            return 1;
        }
        long rc = posix_kmodload(argv[2]);
        if (rc < 0) {
            (void)write_str("kmodctl: load failed\n");
            return 1;
        }
        (void)write_str("kmodctl: loaded ");
        (void)write_str(argv[2]);
        (void)write_str("\n");
        return 0;
    }

    if (streq(argv[1], "unload")) {
        if (argc < 3 || !argv[2]) {
            usage();
            return 1;
        }
        long rc = posix_kmodunload(argv[2]);
        if (rc < 0) {
            (void)write_str("kmodctl: unload failed\n");
            return 1;
        }
        (void)write_str("kmodctl: unloaded ");
        (void)write_str(argv[2]);
        (void)write_str("\n");
        return 0;
    }

    usage();
    return 1;
}
