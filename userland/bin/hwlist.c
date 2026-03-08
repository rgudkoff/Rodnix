/*
 * hwlist.c
 * List discovered hardware devices from Fabric.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "hwinfo.h"

#define FD_STDOUT 1
#define HWLIST_MAX_QUERY 64

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

static void write_hex_u8(uint8_t v)
{
    static const char h[] = "0123456789ABCDEF";
    char out[2];
    out[0] = h[(v >> 4) & 0x0F];
    out[1] = h[v & 0x0F];
    (void)write_buf(out, 2);
}

static void write_hex_u16(uint16_t v)
{
    write_hex_u8((uint8_t)((v >> 8) & 0xFF));
    write_hex_u8((uint8_t)(v & 0xFF));
}

static void write_hex_u32(uint32_t v)
{
    write_hex_u16((uint16_t)((v >> 16) & 0xFFFF));
    write_hex_u16((uint16_t)(v & 0xFFFF));
}

int main(void)
{
    hwdev_info_t devs[HWLIST_MAX_QUERY];
    uint32_t total = 0;
    long n = posix_hwlist(devs, HWLIST_MAX_QUERY, &total);
    if (n < 0) {
        (void)write_str("hwlist: syscall failed\n");
        return 1;
    }

    (void)write_str("hardware devices: ");
    write_u64((uint64_t)total);
    (void)write_str("\n");

    for (long i = 0; i < n; i++) {
        (void)write_str("[");
        write_u64((uint64_t)i);
        (void)write_str("] ");
        (void)write_str(devs[i].name[0] ? devs[i].name : "unnamed");
        (void)write_str(" vendor=0x");
        write_hex_u16(devs[i].vendor_id);
        (void)write_str(" device=0x");
        write_hex_u16(devs[i].device_id);
        (void)write_str(" class=");
        write_hex_u8(devs[i].class_code);
        (void)write_str(":");
        write_hex_u8(devs[i].subclass);
        (void)write_str(":");
        write_hex_u8(devs[i].prog_if);
        (void)write_str(" attached=");
        write_u64((uint64_t)devs[i].attached);
        (void)write_str("\n");

        if (devs[i].is_pci) {
            (void)write_str("    pci bdf=");
            write_u64((uint64_t)devs[i].pci_bus);
            (void)write_str(":");
            write_u64((uint64_t)devs[i].pci_device);
            (void)write_str(".");
            write_u64((uint64_t)devs[i].pci_function);
            (void)write_str(" rev=0x");
            write_hex_u8(devs[i].pci_revision);
            (void)write_str(" hdr=0x");
            write_hex_u8(devs[i].pci_header_type);
            (void)write_str(" cmd=0x");
            write_hex_u16(devs[i].pci_command);
            (void)write_str(" sts=0x");
            write_hex_u16(devs[i].pci_status);
            (void)write_str("\n");

            for (uint32_t bar = 0; bar < HWINFO_BAR_COUNT; bar++) {
                (void)write_str("    BAR");
                write_u64((uint64_t)bar);
                (void)write_str("=0x");
                write_hex_u32(devs[i].bars[bar]);
                (void)write_str("\n");
            }
        }
    }

    if (total > (uint32_t)n) {
        (void)write_str("hwlist: output truncated\n");
    }
    return 0;
}
