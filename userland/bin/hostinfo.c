/*
 * hostinfo.c
 * Compact system report utility (replacement for legacy sysinfo).
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "sysinfo.h"

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

static void write_hex_u32(uint32_t v)
{
    static const char table[] = "0123456789ABCDEF";
    char out[8];
    for (int i = 7; i >= 0; i--) {
        out[i] = table[v & 0x0F];
        v >>= 4;
    }
    (void)write_buf(out, 8);
}

static void write_mem_short(uint64_t bytes)
{
    const uint64_t KB = 1024ULL;
    const uint64_t MB = 1024ULL * 1024ULL;
    if (bytes >= MB) {
        write_u64(bytes / MB);
        (void)write_str(" MB");
        return;
    }
    if (bytes >= KB) {
        write_u64(bytes / KB);
        (void)write_str(" KB");
        return;
    }
    write_u64(bytes);
    (void)write_str(" B");
}

int main(void)
{
    static rodnix_sysinfo_t s;
    long rc = posix_sysinfo(&s);
    if (rc != 0) {
        (void)write_str("hostinfo: syscall failed\n");
        return 1;
    }

    (void)write_str("Host: ");
    (void)write_str(s.sysname);
    (void)write_str(" ");
    (void)write_str(s.release);
    (void)write_str(" (");
    (void)write_str(s.machine);
    (void)write_str(")\n");

    (void)write_str("Build: ");
    (void)write_str(s.version);
    (void)write_str("\n");

    (void)write_str("Uptime: ");
    write_u64(s.uptime_us);
    (void)write_str(" us [");
    (void)write_str(s.uptime_source);
    (void)write_str("]\n");

    (void)write_str("CPU: ");
    (void)write_str(s.cpu_vendor);
    (void)write_str(" | ");
    (void)write_str(s.cpu_model);
    (void)write_str(" | fam/mod/step ");
    write_u64((uint64_t)s.cpu_family);
    (void)write_str("/");
    write_u64((uint64_t)s.cpu_model_id);
    (void)write_str("/");
    write_u64((uint64_t)s.cpu_stepping);
    (void)write_str("\n");

    (void)write_str("Mem: total=");
    write_mem_short(s.mem_total_bytes);
    (void)write_str(" free=");
    write_mem_short(s.mem_free_bytes);
    (void)write_str(" used=");
    write_mem_short(s.mem_used_bytes);
    (void)write_str("\n");

    (void)write_str("IRQ: apic=");
    write_u64((uint64_t)s.apic_available);
    (void)write_str(" ioapic=");
    write_u64((uint64_t)s.ioapic_available);
    (void)write_str("\n");

    (void)write_str("Syscalls: int80=");
    write_u64(s.syscall_int80_count);
    (void)write_str(" fast=");
    write_u64(s.syscall_fast_count);
    (void)write_str("\n");

    (void)write_str("Fabric: buses/drivers/devices/services=");
    write_u64((uint64_t)s.fabric_buses);
    (void)write_str("/");
    write_u64((uint64_t)s.fabric_drivers);
    (void)write_str("/");
    write_u64((uint64_t)s.fabric_devices);
    (void)write_str("/");
    write_u64((uint64_t)s.fabric_services);
    (void)write_str("\n");

    (void)write_str("CPU feat: edx=0x");
    write_hex_u32(s.cpu_features);
    (void)write_str(" ecx=0x");
    write_hex_u32(s.cpu_features_ecx);
    (void)write_str("\n");

    return 0;
}
