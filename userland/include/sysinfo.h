#ifndef _RODNIX_USERLAND_SYSINFO_H
#define _RODNIX_USERLAND_SYSINFO_H

#include <stdint.h>

typedef struct rodnix_sysinfo {
    char sysname[32];
    char release[32];
    char version[64];
    char machine[32];

    uint64_t uptime_us;
    char uptime_source[16];

    uint32_t cpu_id;
    uint32_t apic_id;
    uint32_t cpu_family;
    uint32_t cpu_model_id;
    uint32_t cpu_stepping;
    uint32_t cpu_features;
    uint32_t cpu_features_ecx;
    uint32_t cpu_ext_features_ebx;
    uint32_t cpu_ext_features_ecx;
    uint32_t cpu_cores;
    uint32_t cpu_threads;
    char cpu_vendor[16];
    char cpu_model[64];

    uint64_t mem_total_bytes;
    uint64_t mem_free_bytes;
    uint64_t mem_used_bytes;
    uint64_t pmm_total_pages;
    uint64_t pmm_free_pages;
    uint64_t pmm_used_pages;
    uint64_t oom_pmm;
    uint64_t oom_vmm;
    uint64_t oom_heap;

    uint32_t fabric_buses;
    uint32_t fabric_drivers;
    uint32_t fabric_devices;
    uint32_t fabric_services;

    uint8_t apic_available;
    uint8_t ioapic_available;
    uint8_t reserved0;
    uint8_t reserved1;
} rodnix_sysinfo_t;

#endif /* _RODNIX_USERLAND_SYSINFO_H */
