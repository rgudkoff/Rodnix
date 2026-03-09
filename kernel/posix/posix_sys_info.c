#include "posix_sys_info.h"
#include "posix_syscall.h"
#include "posix_uapi_compat.h"
#include "../core/cpu.h"
#include "../core/memory.h"
#include "../common/syscall.h"
#include "../common/kmod.h"
#include "../fabric/fabric.h"
#include "../fabric/device/device.h"
#include "../fabric/service/net_service.h"
#include "../fabric/service/block_service.h"
#include "../unix/unix_layer.h"
#include "../../include/error.h"
#include "../../include/version.h"
#include "../../include/utsname.h"
#include "../../include/common.h"
#include "../../include/console.h"
#include "../arch/x86_64/config.h"
#include <stddef.h>

uint64_t posix_uname(uint64_t a1,
                            uint64_t a2,
                            uint64_t a3,
                            uint64_t a4,
                            uint64_t a5,
                            uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    utsname_t* u = (utsname_t*)(uintptr_t)a1;
    if (!unix_user_range_ok(u, sizeof(*u))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    memset(u, 0, sizeof(*u));
    u->hdr = RDNX_ABI_INIT(utsname_t);
    strncpy(u->sysname, RODNIX_SYSNAME, sizeof(u->sysname) - 1);
    strncpy(u->nodename, RODNIX_NODENAME, sizeof(u->nodename) - 1);
    strncpy(u->release, RODNIX_RELEASE, sizeof(u->release) - 1);
    strncpy(u->version, RODNIX_VERSION, sizeof(u->version) - 1);
    strncpy(u->machine, X86_64_MACHINE, sizeof(u->machine) - 1);
    return (uint64_t)RDNX_OK;
}

uint64_t posix_netiflist(uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    fabric_netif_info_t* user_entries = (fabric_netif_info_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;

    if (max_entries == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(fabric_netif_info_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t total = fabric_netif_count();
    uint32_t n = (total < max_entries) ? total : max_entries;
    for (uint32_t i = 0; i < n; i++) {
        fabric_netif_info_t info;
        if (fabric_netif_get_info(i, &info) != RDNX_OK) {
            break;
        }
        user_entries[i] = info;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

uint64_t posix_hwlist(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    hwdev_info_t* user_entries = (hwdev_info_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;

    if (max_entries == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(hwdev_info_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t total = fabric_device_count();
    uint32_t n = (total < max_entries) ? total : max_entries;
    for (uint32_t i = 0; i < n; i++) {
        fabric_device_t* dev = fabric_device_get(i);
        if (!dev) {
            break;
        }

        hwdev_info_t info;
        memset(&info, 0, sizeof(info));
        if (dev->name) {
            strncpy(info.name, dev->name, sizeof(info.name) - 1);
        }
        info.vendor_id = dev->vendor_id;
        info.device_id = dev->device_id;
        info.class_code = dev->class_code;
        info.subclass = dev->subclass;
        info.prog_if = dev->prog_if;
        info.attached = dev->driver_state ? 1u : 0u;

        if (dev->bus_private && dev->name && strcmp(dev->name, "pci-device") == 0) {
            const pci_device_info_t* pci = (const pci_device_info_t*)dev->bus_private;
            info.is_pci = 1u;
            info.pci_bus = pci->bus;
            info.pci_device = pci->device;
            info.pci_function = pci->function;
            info.pci_revision = pci->revision_id;
            info.pci_header_type = pci->header_type;
            info.pci_command = pci->command;
            info.pci_status = pci->status;
            for (uint32_t bar = 0; bar < PCI_BAR_COUNT; bar++) {
                info.bars[bar] = pci->bars[bar];
            }
        }

        user_entries[i] = info;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

uint64_t posix_fabricls(uint64_t a1,
                               uint64_t a2,
                               uint64_t a3,
                               uint64_t a4,
                               uint64_t a5,
                               uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;
    fabric_node_info_t* user_entries = (fabric_node_info_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;

    if (max_entries == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(fabric_node_info_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t total = 0;
    int n = fabric_node_list(user_entries, max_entries, &total);
    if (n < 0) {
        return (uint64_t)n;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

uint64_t posix_fabricevents(uint64_t a1,
                                   uint64_t a2,
                                   uint64_t a3,
                                   uint64_t a4,
                                   uint64_t a5,
                                   uint64_t a6)
{
    (void)a5;
    (void)a6;
    fabric_event_t* user_entries = (fabric_event_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_read = (uint32_t*)(uintptr_t)a3;
    uint32_t* user_dropped = (uint32_t*)(uintptr_t)a4;

    if (max_entries == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(fabric_event_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_read && !unix_user_range_ok(user_read, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_dropped && !unix_user_range_ok(user_dropped, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint32_t read = 0;
    uint32_t dropped = 0;
    int rc = fabric_event_drain(user_entries, max_entries, &read, &dropped);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    if (user_read) {
        *user_read = read;
    }
    if (user_dropped) {
        *user_dropped = dropped;
    }
    return (uint64_t)read;
}

uint64_t posix_sysinfo(uint64_t a1,
                              uint64_t a2,
                              uint64_t a3,
                              uint64_t a4,
                              uint64_t a5,
                              uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    rodnix_sysinfo_t* out = (rodnix_sysinfo_t*)(uintptr_t)a1;
    if (!unix_user_range_ok(out, sizeof(*out))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    memset(out, 0, sizeof(*out));

    strncpy(out->sysname, RODNIX_SYSNAME, sizeof(out->sysname) - 1);
    strncpy(out->release, RODNIX_RELEASE, sizeof(out->release) - 1);
    strncpy(out->version, RODNIX_VERSION, sizeof(out->version) - 1);
    strncpy(out->machine, X86_64_MACHINE, sizeof(out->machine) - 1);
    out->uptime_us = console_get_uptime_us();
    {
        const char* src = console_get_uptime_source();
        if (src) {
            strncpy(out->uptime_source, src, sizeof(out->uptime_source) - 1);
        }
    }

    cpu_info_t cinfo;
    if (cpu_get_info(&cinfo) == 0) {
        out->cpu_id = cinfo.cpu_id;
        out->apic_id = cinfo.apic_id;
        out->cpu_family = cinfo.family;
        out->cpu_model_id = cinfo.model_id;
        out->cpu_stepping = cinfo.stepping;
        out->cpu_features = cinfo.features;
        out->cpu_features_ecx = cinfo.features_ecx;
        out->cpu_ext_features_ebx = cinfo.ext_features_ebx;
        out->cpu_ext_features_ecx = cinfo.ext_features_ecx;
        out->cpu_cores = cinfo.cores;
        out->cpu_threads = cinfo.threads;
        if (cinfo.vendor) {
            strncpy(out->cpu_vendor, cinfo.vendor, sizeof(out->cpu_vendor) - 1);
        }
        if (cinfo.model) {
            strncpy(out->cpu_model, cinfo.model, sizeof(out->cpu_model) - 1);
        }
    }

    memory_info_t minfo;
    if (memory_get_info(&minfo) == RDNX_OK) {
        out->mem_total_bytes = minfo.total_physical;
        out->mem_free_bytes = minfo.free_physical;
        out->mem_used_bytes = minfo.used_physical;
        out->oom_pmm = minfo.oom_pmm;
        out->oom_vmm = minfo.oom_vmm;
        out->oom_heap = minfo.oom_heap;
    }

    extern uint64_t pmm_get_total_pages(void);
    extern uint64_t pmm_get_free_pages(void);
    extern uint64_t pmm_get_used_pages(void);
    out->pmm_total_pages = pmm_get_total_pages();
    out->pmm_free_pages = pmm_get_free_pages();
    out->pmm_used_pages = pmm_get_used_pages();

    fabric_stats_t fstats;
    if (fabric_get_stats(&fstats) == RDNX_OK) {
        out->fabric_buses = fstats.buses;
        out->fabric_drivers = fstats.drivers;
        out->fabric_devices = fstats.devices;
        out->fabric_services = fstats.services;
    }

    extern bool apic_is_available(void);
    extern bool ioapic_is_available(void);
    out->apic_available = apic_is_available() ? 1u : 0u;
    out->ioapic_available = ioapic_is_available() ? 1u : 0u;
    out->syscall_int80_count = syscall_get_int80_count();
    out->syscall_fast_count = syscall_get_fast_count();

    return (uint64_t)RDNX_OK;
}

uint64_t posix_scstat(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;

    rodnix_scstat_entry_t* user_entries = (rodnix_scstat_entry_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;
    uint32_t total = (uint32_t)(POSIX_SYS_LAST + 1u);
    uint32_t n = (max_entries < total) ? max_entries : total;

    if (max_entries == 0 || !user_entries) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(*user_entries))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < n; i++) {
        uint64_t int80 = syscall_get_int80_count_for_num(i);
        uint64_t fast = syscall_get_fast_count_for_num(i);
        rodnix_scstat_entry_t e;
        e.syscall_no = i;
        e.reserved0 = 0;
        e.int80_count = int80;
        e.fast_count = fast;
        e.total_count = int80 + fast;
        user_entries[i] = e;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

uint64_t posix_blocklist(uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;

    rodnix_blockdev_info_t* user_entries = (rodnix_blockdev_info_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;
    uint32_t total = fabric_blockdev_count();
    uint32_t n = (max_entries < total) ? max_entries : total;

    if (max_entries == 0 || !user_entries) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(*user_entries))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < n; i++) {
        fabric_blockdev_info_t info;
        if (fabric_blockdev_get_info(i, &info) != RDNX_OK) {
            continue;
        }
        rodnix_blockdev_info_t out;
        memset(&out, 0, sizeof(out));
        strncpy(out.name, info.name, sizeof(out.name) - 1);
        out.sector_size = info.sector_size;
        out.sector_count = info.sector_count;
        out.flags = info.flags;
        user_entries[i] = out;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

uint64_t posix_blockread(uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    (void)a5;
    (void)a6;

    const char* name = (const char*)(uintptr_t)a1;
    uint64_t lba = a2;
    void* out = (void*)(uintptr_t)a3;
    uint64_t out_len = a4;

    if (!name || !out || out_len == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(name, 1) || !unix_user_range_ok(out, (size_t)out_len)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    fabric_blockdev_t* dev = fabric_blockdev_find(name);
    if (!dev) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    if (dev->sector_size == 0 || dev->sector_size > out_len || dev->sector_size > 4096u) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint8_t bounce[4096];
    int rc = fabric_blockdev_read(dev, lba, 1, bounce);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    memcpy(out, bounce, dev->sector_size);
    return (uint64_t)dev->sector_size;
}

uint64_t posix_blockwrite(uint64_t a1,
                                 uint64_t a2,
                                 uint64_t a3,
                                 uint64_t a4,
                                 uint64_t a5,
                                 uint64_t a6)
{
    (void)a5;
    (void)a6;

    const char* name = (const char*)(uintptr_t)a1;
    uint64_t lba = a2;
    const void* in = (const void*)(uintptr_t)a3;
    uint64_t in_len = a4;

    if (!name || !in || in_len == 0) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(name, 1) || !unix_user_range_ok(in, (size_t)in_len)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    fabric_blockdev_t* dev = fabric_blockdev_find(name);
    if (!dev) {
        return (uint64_t)RDNX_E_NOTFOUND;
    }
    if (dev->sector_size == 0 || dev->sector_size > in_len || dev->sector_size > 4096u) {
        return (uint64_t)RDNX_E_INVALID;
    }

    uint8_t bounce[4096];
    memcpy(bounce, in, dev->sector_size);
    int rc = fabric_blockdev_write(dev, lba, 1, bounce);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    return (uint64_t)dev->sector_size;
}

uint64_t posix_kmodls(uint64_t a1,
                             uint64_t a2,
                             uint64_t a3,
                             uint64_t a4,
                             uint64_t a5,
                             uint64_t a6)
{
    (void)a4;
    (void)a5;
    (void)a6;

    rodnix_kmod_info_t* user_entries = (rodnix_kmod_info_t*)(uintptr_t)a1;
    uint32_t max_entries = (uint32_t)a2;
    uint32_t* user_count = (uint32_t*)(uintptr_t)a3;
    uint32_t total = kmod_count();
    uint32_t n = (max_entries < total) ? max_entries : total;

    if (max_entries == 0 || !user_entries) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_entries, (size_t)max_entries * sizeof(*user_entries))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (user_count && !unix_user_range_ok(user_count, sizeof(uint32_t))) {
        return (uint64_t)RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < n; i++) {
        kmod_info_t ki;
        if (kmod_get_info(i, &ki) != RDNX_OK) {
            continue;
        }
        rodnix_kmod_info_t out;
        memset(&out, 0, sizeof(out));
        strncpy(out.name, ki.name, sizeof(out.name) - 1);
        strncpy(out.kind, ki.kind, sizeof(out.kind) - 1);
        strncpy(out.version, ki.version, sizeof(out.version) - 1);
        out.flags = ki.flags;
        out.builtin = ki.builtin;
        out.loaded = ki.loaded;
        user_entries[i] = out;
    }
    if (user_count) {
        *user_count = total;
    }
    return (uint64_t)n;
}

uint64_t posix_kmodload(uint64_t a1,
                               uint64_t a2,
                               uint64_t a3,
                               uint64_t a4,
                               uint64_t a5,
                               uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    const char* path = (const char*)(uintptr_t)a1;
    if (!path || !unix_user_range_ok(path, 1)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)kmod_load(path);
}

uint64_t posix_kmodunload(uint64_t a1,
                                 uint64_t a2,
                                 uint64_t a3,
                                 uint64_t a4,
                                 uint64_t a5,
                                 uint64_t a6)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    const char* name = (const char*)(uintptr_t)a1;
    if (!name || !unix_user_range_ok(name, 1)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)kmod_unload(name);
}

uint64_t posix_clock_gettime(uint64_t a1,
                                    uint64_t a2,
                                    uint64_t a3,
                                    uint64_t a4,
                                    uint64_t a5,
                                    uint64_t a6)
{
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    enum {
        CLOCK_REALTIME = 0,
        CLOCK_MONOTONIC = 4,
        CLOCK_MONOTONIC_ALT = 1
    };
    int clock_id = (int)a1;
    rdnx_timespec_t* out = (rdnx_timespec_t*)(uintptr_t)a2;
    if (!unix_user_range_ok(out, sizeof(*out))) {
        return (uint64_t)RDNX_E_INVALID;
    }
    uint64_t us = 0;
    if (clock_id == CLOCK_MONOTONIC || clock_id == CLOCK_MONOTONIC_ALT) {
        us = console_get_uptime_us();
    } else if (clock_id == CLOCK_REALTIME) {
        us = console_get_realtime_us();
    } else {
        /* Be permissive for early userland ABI drift: treat unknown clocks as monotonic. */
        us = console_get_uptime_us();
    }
    out->tv_sec = (int64_t)(us / 1000000ULL);
    out->tv_nsec = (int64_t)((us % 1000000ULL) * 1000ULL);
    return (uint64_t)RDNX_OK;
}

uint64_t posix_nanosleep(uint64_t a1,
                                uint64_t a2,
                                uint64_t a3,
                                uint64_t a4,
                                uint64_t a5,
                                uint64_t a6)
{
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    return unix_time_nanosleep(a1, a2);
}
