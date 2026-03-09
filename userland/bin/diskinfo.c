/*
 * diskinfo.c
 * Block device inventory and basic sector read probe.
 */

#include <stdint.h>
#include "posix_syscall.h"
#include "diskinfo.h"

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

static void write_hex8(uint8_t v)
{
    static const char* hx = "0123456789ABCDEF";
    char out[2];
    out[0] = hx[(v >> 4) & 0x0F];
    out[1] = hx[v & 0x0F];
    (void)write_buf(out, 2);
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

static uint64_t parse_u64(const char* s)
{
    uint64_t out = 0;
    if (!s) {
        return 0;
    }
    for (uint64_t i = 0; s[i]; i++) {
        char c = s[i];
        if (c < '0' || c > '9') {
            return 0;
        }
        out = (out * 10u) + (uint64_t)(c - '0');
    }
    return out;
}

static void print_usage(void)
{
    (void)write_str("usage:\n");
    (void)write_str("  diskinfo\n");
    (void)write_str("  diskinfo -r <device> <lba>\n");
}

static void dump_sector_prefix(const uint8_t* data, uint32_t count)
{
    uint32_t n = (count < 128u) ? count : 128u;
    for (uint32_t i = 0; i < n; i++) {
        if ((i % 16u) == 0) {
            (void)write_str("  ");
            write_u64((uint64_t)i);
            (void)write_str(": ");
        }
        write_hex8(data[i]);
        if ((i % 16u) == 15u) {
            (void)write_str("\n");
        } else {
            (void)write_str(" ");
        }
    }
    if ((n % 16u) != 0u) {
        (void)write_str("\n");
    }
}

int main(int argc, char** argv)
{
    rodnix_blockdev_info_t devs[16];
    uint32_t total = 0;
    long n = posix_blocklist(devs, 16, &total);
    if (n < 0) {
        (void)write_str("diskinfo: blocklist syscall failed\n");
        return 1;
    }

    if (argc == 4 && streq(argv[1], "-r")) {
        uint8_t sector[4096];
        const char* name = argv[2];
        uint64_t lba = parse_u64(argv[3]);
        long bytes = posix_blockread(name, lba, sector, sizeof(sector));
        if (bytes < 0) {
            (void)write_str("diskinfo: blockread failed\n");
            return 1;
        }
        (void)write_str("diskinfo: read ");
        (void)write_str(name);
        (void)write_str(" lba=");
        write_u64(lba);
        (void)write_str(" bytes=");
        write_u64((uint64_t)bytes);
        (void)write_str("\n");
        dump_sector_prefix(sector, (uint32_t)bytes);
        return 0;
    }

    if (argc != 1) {
        print_usage();
        return 1;
    }

    (void)write_str("diskinfo: ");
    write_u64((uint64_t)n);
    (void)write_str("/");
    write_u64((uint64_t)total);
    (void)write_str(" devices\n");

    for (uint32_t i = 0; i < (uint32_t)n; i++) {
        uint64_t bytes = (uint64_t)devs[i].sector_size * devs[i].sector_count;
        (void)write_str("  ");
        (void)write_str(devs[i].name);
        (void)write_str(": sector_size=");
        write_u64((uint64_t)devs[i].sector_size);
        (void)write_str(" sectors=");
        write_u64(devs[i].sector_count);
        (void)write_str(" bytes=");
        write_u64(bytes);
        if (devs[i].flags & 1u) {
            (void)write_str(" ro");
        } else {
            (void)write_str(" rw");
        }
        (void)write_str("\n");
    }

    return 0;
}
