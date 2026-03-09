#ifndef _RODNIX_POSIX_UAPI_COMPAT_H
#define _RODNIX_POSIX_UAPI_COMPAT_H

#include <stdint.h>
#include "../fabric/bus/pci.h"

typedef struct hwdev_info {
    char name[32];
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t attached;
    uint8_t is_pci;
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint8_t pci_revision;
    uint8_t pci_header_type;
    uint16_t pci_command;
    uint16_t pci_status;
    uint32_t bars[PCI_BAR_COUNT];
} hwdev_info_t;

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

    uint64_t syscall_int80_count;
    uint64_t syscall_fast_count;
} rodnix_sysinfo_t;

typedef struct rdnx_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} rdnx_timespec_t;

typedef struct rodnix_scstat_entry {
    uint32_t syscall_no;
    uint32_t reserved0;
    uint64_t int80_count;
    uint64_t fast_count;
    uint64_t total_count;
} rodnix_scstat_entry_t;

typedef struct rodnix_blockdev_info {
    char name[16];
    uint32_t sector_size;
    uint64_t sector_count;
    uint32_t flags;
    uint32_t reserved0;
} rodnix_blockdev_info_t;

typedef struct rodnix_kmod_info {
    char name[32];
    char kind[16];
    char version[16];
    uint32_t flags;
    uint8_t builtin;
    uint8_t loaded;
    uint8_t reserved0;
    uint8_t reserved1;
} rodnix_kmod_info_t;

#endif /* _RODNIX_POSIX_UAPI_COMPAT_H */
