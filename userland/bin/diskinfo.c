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

static int parse_u8(const char* s, uint8_t* out)
{
    uint64_t v = 0;
    if (!s || !out || !s[0]) {
        return 0;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (uint64_t i = 2; s[i]; i++) {
            char c = s[i];
            uint8_t nibble = 0;
            if (c >= '0' && c <= '9') {
                nibble = (uint8_t)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nibble = (uint8_t)(10 + (c - 'a'));
            } else if (c >= 'A' && c <= 'F') {
                nibble = (uint8_t)(10 + (c - 'A'));
            } else {
                return 0;
            }
            v = (v << 4) | nibble;
            if (v > 255u) {
                return 0;
            }
        }
    } else {
        v = parse_u64(s);
        if (v > 255u) {
            return 0;
        }
    }
    *out = (uint8_t)v;
    return 1;
}

static void copy_cstr(char* dst, uint64_t cap, const char* src)
{
    uint64_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void join_dev_path(char* out, uint64_t cap, const char* name)
{
    if (!out || cap == 0) {
        return;
    }
    if (!name) {
        out[0] = '\0';
        return;
    }
    if (name[0] == '/') {
        copy_cstr(out, cap, name);
        return;
    }
    const char* prefix = "/dev/";
    uint64_t p = 0;
    for (uint64_t i = 0; prefix[i] && p + 1 < cap; i++) {
        out[p++] = prefix[i];
    }
    for (uint64_t i = 0; name[i] && p + 1 < cap; i++) {
        out[p++] = name[i];
    }
    out[p] = '\0';
}

static int hexdump_path(const char* path, uint64_t off, uint64_t len)
{
    enum { VFS_OPEN_READ = 1 };
    uint8_t buf[128];
    long fd;
    uint64_t done = 0;

    if (!path || len == 0) {
        return 1;
    }
    fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        (void)write_str("diskinfo: open failed\n");
        return 1;
    }
    if (off > 0) {
        long s = posix_lseek((int)fd, (long)off, 0);
        if (s < 0) {
            (void)posix_close((int)fd);
            (void)write_str("diskinfo: lseek failed\n");
            return 1;
        }
    }

    (void)write_str("diskinfo: hexdump ");
    (void)write_str(path);
    (void)write_str(" off=");
    write_u64(off);
    (void)write_str(" len=");
    write_u64(len);
    (void)write_str("\n");

    while (done < len) {
        uint64_t want = len - done;
        if (want > sizeof(buf)) {
            want = sizeof(buf);
        }
        long n = posix_read((int)fd, buf, want);
        if (n <= 0) {
            break;
        }
        for (uint64_t i = 0; i < (uint64_t)n; i++) {
            uint64_t idx = done + i;
            if ((idx % 16u) == 0) {
                (void)write_str("  ");
                write_u64(idx);
                (void)write_str(": ");
            }
            write_hex8(buf[i]);
            if ((idx % 16u) == 15u) {
                (void)write_str("\n");
            } else {
                (void)write_str(" ");
            }
        }
        done += (uint64_t)n;
    }
    if ((done % 16u) != 0u) {
        (void)write_str("\n");
    }
    (void)posix_close((int)fd);
    return 0;
}

static void print_usage(void)
{
    (void)write_str("usage:\n");
    (void)write_str("  diskinfo\n");
    (void)write_str("  diskinfo -r <device> <lba>\n");
    (void)write_str("  diskinfo -w <device> <lba> <byte(0..255|0xNN)>\n");
    (void)write_str("  diskinfo -x <device|/path> <offset> <len>\n");
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
    if (argc == 5 && streq(argv[1], "-w")) {
        enum { SECTOR_MAX = 4096 };
        uint8_t sector[SECTOR_MAX];
        uint8_t check[SECTOR_MAX];
        const char* name = argv[2];
        uint64_t lba = parse_u64(argv[3]);
        uint8_t value = 0;
        if (!parse_u8(argv[4], &value)) {
            (void)write_str("diskinfo: invalid byte value\n");
            return 1;
        }
        uint32_t sector_size = 512u;
        for (uint32_t i = 0; i < (uint32_t)n; i++) {
            if (streq(devs[i].name, name)) {
                sector_size = devs[i].sector_size;
                break;
            }
        }
        if (sector_size == 0 || sector_size > SECTOR_MAX) {
            (void)write_str("diskinfo: unsupported sector size\n");
            return 1;
        }
        for (uint32_t i = 0; i < sector_size; i++) {
            sector[i] = value;
        }

        long w = posix_blockwrite(name, lba, sector, sector_size);
        if (w < 0) {
            (void)write_str("diskinfo: blockwrite failed\n");
            return 1;
        }
        long r = posix_blockread(name, lba, check, sector_size);
        if (r < 0) {
            (void)write_str("diskinfo: verify read failed\n");
            return 1;
        }
        for (uint32_t i = 0; i < sector_size; i++) {
            if (check[i] != value) {
                (void)write_str("diskinfo: verify mismatch\n");
                return 1;
            }
        }
        (void)write_str("diskinfo: wrote ");
        (void)write_str(name);
        (void)write_str(" lba=");
        write_u64(lba);
        (void)write_str(" pattern=0x");
        write_hex8(value);
        (void)write_str(" bytes=");
        write_u64((uint64_t)w);
        (void)write_str(" verify=ok\n");
        return 0;
    }
    if (argc == 5 && streq(argv[1], "-x")) {
        char path[64];
        join_dev_path(path, sizeof(path), argv[2]);
        return hexdump_path(path, parse_u64(argv[3]), parse_u64(argv[4]));
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
