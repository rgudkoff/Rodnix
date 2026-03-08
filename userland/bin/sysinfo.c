/*
 * sysinfo.c
 * Full system information utility.
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
    static const char h[] = "0123456789ABCDEF";
    char out[8];
    for (int i = 7; i >= 0; i--) {
        out[i] = h[v & 0x0F];
        v >>= 4;
    }
    (void)write_buf(out, 8);
}

static void write_uptime(uint64_t us)
{
    uint64_t sec = us / 1000000ULL;
    uint64_t ms = (us % 1000000ULL) / 1000ULL;
    uint64_t mins = sec / 60ULL;
    uint64_t hours = mins / 60ULL;
    uint64_t days = hours / 24ULL;
    sec %= 60ULL;
    mins %= 60ULL;
    hours %= 24ULL;
    if (days == 0 && hours == 0 && mins == 0) {
        write_u64(sec);
        (void)write_str(".");
        if (ms < 100) (void)write_str("0");
        if (ms < 10) (void)write_str("0");
        write_u64(ms);
        (void)write_str("s");
        return;
    }

    write_u64(days);
    (void)write_str("d ");
    write_u64(hours);
    (void)write_str("h ");
    write_u64(mins);
    (void)write_str("m ");
    write_u64(sec);
    (void)write_str("s");
}

static void write_mem_dynamic(uint64_t bytes)
{
    const uint64_t KB = 1024ULL;
    const uint64_t MB = 1024ULL * 1024ULL;
    const uint64_t GB = 1024ULL * 1024ULL * 1024ULL;

    /*
     * Prefer MB for "about 1GB" systems too, because reported usable memory
     * can be slightly below 1 GiB after firmware/kernel reservations.
     */
    if (bytes >= (GB / 2ULL)) {
        write_u64(bytes / MB);
        (void)write_str(" MB");
        return;
    }
    if (bytes >= MB) {
        write_u64(bytes / KB);
        (void)write_str(" KB");
        return;
    }

    write_u64(bytes);
    (void)write_str(" B");
}

int main(void)
{
    rodnix_sysinfo_t s;
    long rc = posix_sysinfo(&s);
    if (rc != 0) {
        (void)write_str("sysinfo: syscall failed\n");
        return 1;
    }

    (void)write_str("OS: ");
    (void)write_str(s.sysname);
    (void)write_str(" ");
    (void)write_str(s.release);
    (void)write_str("\nVersion: ");
    (void)write_str(s.version);
    (void)write_str("\nMachine: ");
    (void)write_str(s.machine);
    (void)write_str("\nUptime: ");
    write_uptime(s.uptime_us);
    (void)write_str("\nUptime source: ");
    (void)write_str(s.uptime_source);
    (void)write_str("\nUptime (us): ");
    write_u64(s.uptime_us);
    (void)write_str("\n\nCPU:\n  vendor: ");
    (void)write_str(s.cpu_vendor);
    (void)write_str("\n  model: ");
    (void)write_str(s.cpu_model);
    (void)write_str("\n  cpu_id: ");
    write_u64((uint64_t)s.cpu_id);
    (void)write_str("\n  apic_id: ");
    write_u64((uint64_t)s.apic_id);
    (void)write_str("\n  family/model/stepping: ");
    write_u64((uint64_t)s.cpu_family);
    (void)write_str("/");
    write_u64((uint64_t)s.cpu_model_id);
    (void)write_str("/");
    write_u64((uint64_t)s.cpu_stepping);
    (void)write_str("\n  cores/threads: ");
    write_u64((uint64_t)s.cpu_cores);
    (void)write_str("/");
    write_u64((uint64_t)s.cpu_threads);
    (void)write_str("\n  features(leaf1 edx): 0x");
    write_hex_u32(s.cpu_features);
    (void)write_str("\n  features(leaf1 ecx): 0x");
    write_hex_u32(s.cpu_features_ecx);
    (void)write_str("\n  features(leaf7 ebx): 0x");
    write_hex_u32(s.cpu_ext_features_ebx);
    (void)write_str("\n  features(leaf7 ecx): 0x");
    write_hex_u32(s.cpu_ext_features_ecx);

    (void)write_str("\n\nMemory:\n  total: ");
    write_mem_dynamic(s.mem_total_bytes);
    (void)write_str("\n  free:  ");
    write_mem_dynamic(s.mem_free_bytes);
    (void)write_str("\n  used:  ");
    write_mem_dynamic(s.mem_used_bytes);
    (void)write_str("\n  bytes total/free/used: ");
    write_u64(s.mem_total_bytes);
    (void)write_str("/");
    write_u64(s.mem_free_bytes);
    (void)write_str("/");
    write_u64(s.mem_used_bytes);
    (void)write_str("\n  pages total/free/used: ");
    write_u64(s.pmm_total_pages);
    (void)write_str("/");
    write_u64(s.pmm_free_pages);
    (void)write_str("/");
    write_u64(s.pmm_used_pages);
    (void)write_str("\n  oom pmm/vmm/heap: ");
    write_u64(s.oom_pmm);
    (void)write_str("/");
    write_u64(s.oom_vmm);
    (void)write_str("/");
    write_u64(s.oom_heap);

    (void)write_str("\n\nInterrupts:\n  apic: ");
    write_u64((uint64_t)s.apic_available);
    (void)write_str("\n  ioapic: ");
    write_u64((uint64_t)s.ioapic_available);

    (void)write_str("\n\nFabric:\n  buses/drivers/devices/services: ");
    write_u64((uint64_t)s.fabric_buses);
    (void)write_str("/");
    write_u64((uint64_t)s.fabric_drivers);
    (void)write_str("/");
    write_u64((uint64_t)s.fabric_devices);
    (void)write_str("/");
    write_u64((uint64_t)s.fabric_services);
    (void)write_str("\n");

    return 0;
}
