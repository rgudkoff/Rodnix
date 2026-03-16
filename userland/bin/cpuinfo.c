/*
 * cpuinfo.c
 * Detailed CPU report utility.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "sysinfo.h"

#define FD_STDOUT 1

typedef struct cpu_flag_desc {
    uint32_t bit;
    const char* name;
} cpu_flag_desc_t;

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
        out[i] = table[v & 0x0Fu];
        v >>= 4;
    }
    (void)write_buf(out, 8);
}

static void write_freq(uint64_t hz)
{
    if (hz == 0) {
        (void)write_str("unknown");
        return;
    }
    if (hz >= 1000000000ULL) {
        write_u64(hz / 1000000000ULL);
        (void)write_str(" GHz");
        return;
    }
    if (hz >= 1000000ULL) {
        write_u64(hz / 1000000ULL);
        (void)write_str(" MHz");
        return;
    }
    if (hz >= 1000ULL) {
        write_u64(hz / 1000ULL);
        (void)write_str(" KHz");
        return;
    }
    write_u64(hz);
    (void)write_str(" Hz");
}

static void write_flag_list(const cpu_flag_desc_t* flags, uint32_t count, uint32_t value)
{
    int printed = 0;
    for (uint32_t i = 0; i < count; i++) {
        if ((value & (1u << flags[i].bit)) == 0) {
            continue;
        }
        if (printed != 0) {
            (void)write_str(" ");
        }
        (void)write_str(flags[i].name);
        printed = 1;
    }
    if (!printed) {
        (void)write_str("(none)");
    }
    (void)write_str("\n");
}

static const cpu_flag_desc_t leaf1_edx_flags[] = {
    {0, "fpu"}, {4, "tsc"}, {5, "msr"}, {8, "cx8"}, {9, "apic"},
    {11, "sep"}, {13, "pge"}, {15, "cmov"}, {19, "clflush"}, {23, "mmx"},
    {24, "fxsr"}, {25, "sse"}, {26, "sse2"}, {28, "htt"}
};

static const cpu_flag_desc_t leaf1_ecx_flags[] = {
    {0, "sse3"}, {1, "pclmulqdq"}, {9, "ssse3"}, {12, "fma"}, {13, "cx16"},
    {19, "sse4.1"}, {20, "sse4.2"}, {22, "movbe"}, {23, "popcnt"},
    {25, "aes"}, {26, "xsave"}, {27, "osxsave"}, {28, "avx"}, {29, "f16c"},
    {30, "rdrand"}
};

static const cpu_flag_desc_t leaf7_ebx_flags[] = {
    {0, "fsgsbase"}, {3, "bmi1"}, {4, "hle"}, {5, "avx2"}, {7, "smep"},
    {8, "bmi2"}, {9, "erms"}, {10, "invpcid"}, {11, "rtm"}, {14, "mpx"},
    {16, "avx512f"}, {18, "rdseed"}, {19, "adx"}, {20, "smap"}, {29, "sha"}
};

static const cpu_flag_desc_t leaf7_ecx_flags[] = {
    {1, "avx512vbmi"}, {3, "pku"}, {4, "ospke"}, {5, "waitpkg"},
    {8, "gfni"}, {9, "vaes"}, {10, "vpclmulqdq"}, {11, "avx512vnni"},
    {22, "rdpid"}
};

int main(void)
{
    rodnix_sysinfo_t s;
    long rc = posix_sysinfo(&s);
    if (rc != 0) {
        (void)write_str("cpuinfo: syscall failed\n");
        return 1;
    }

    (void)write_str("CPU Summary\n");
    (void)write_str("  vendor: ");
    (void)write_str(s.cpu_vendor);
    (void)write_str("\n  model: ");
    (void)write_str(s.cpu_model);
    (void)write_str("\n  arch: ");
    (void)write_str(s.machine);
    (void)write_str("\n  frequency: ");
    write_freq(s.cpu_freq_hz);
    (void)write_str(" (");
    write_u64(s.cpu_freq_hz);
    (void)write_str(" Hz)\n");

    (void)write_str("\nTopology\n");
    (void)write_str("  current cpu id: ");
    write_u64((uint64_t)s.cpu_id);
    (void)write_str("\n  current apic id: ");
    write_u64((uint64_t)s.apic_id);
    (void)write_str("\n  online cpus seen by kernel: ");
    write_u64((uint64_t)s.cpu_count);
    (void)write_str("\n  cores per package: ");
    write_u64((uint64_t)s.cpu_cores);
    (void)write_str("\n  threads per package: ");
    write_u64((uint64_t)s.cpu_threads);
    (void)write_str("\n");

    (void)write_str("\nIdentification\n");
    (void)write_str("  family: ");
    write_u64((uint64_t)s.cpu_family);
    (void)write_str("\n  model id: ");
    write_u64((uint64_t)s.cpu_model_id);
    (void)write_str("\n  stepping: ");
    write_u64((uint64_t)s.cpu_stepping);
    (void)write_str("\n");

    (void)write_str("\nFeature Registers\n");
    (void)write_str("  CPUID.1:EDX = 0x");
    write_hex_u32(s.cpu_features);
    (void)write_str("\n  CPUID.1:ECX = 0x");
    write_hex_u32(s.cpu_features_ecx);
    (void)write_str("\n  CPUID.7:EBX = 0x");
    write_hex_u32(s.cpu_ext_features_ebx);
    (void)write_str("\n  CPUID.7:ECX = 0x");
    write_hex_u32(s.cpu_ext_features_ecx);
    (void)write_str("\n");

    (void)write_str("\nDecoded Features\n");
    (void)write_str("  leaf1 edx: ");
    write_flag_list(leaf1_edx_flags,
                    (uint32_t)(sizeof(leaf1_edx_flags) / sizeof(leaf1_edx_flags[0])),
                    s.cpu_features);
    (void)write_str("  leaf1 ecx: ");
    write_flag_list(leaf1_ecx_flags,
                    (uint32_t)(sizeof(leaf1_ecx_flags) / sizeof(leaf1_ecx_flags[0])),
                    s.cpu_features_ecx);
    (void)write_str("  leaf7 ebx: ");
    write_flag_list(leaf7_ebx_flags,
                    (uint32_t)(sizeof(leaf7_ebx_flags) / sizeof(leaf7_ebx_flags[0])),
                    s.cpu_ext_features_ebx);
    (void)write_str("  leaf7 ecx: ");
    write_flag_list(leaf7_ecx_flags,
                    (uint32_t)(sizeof(leaf7_ecx_flags) / sizeof(leaf7_ecx_flags[0])),
                    s.cpu_ext_features_ecx);

    return 0;
}
