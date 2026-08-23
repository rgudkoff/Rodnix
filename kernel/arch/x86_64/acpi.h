/**
 * @file acpi.h
 * @brief Basic ACPI discovery/parsing interface for x86_64.
 */

#ifndef _RODNIX_ARCH_X86_64_ACPI_H
#define _RODNIX_ARCH_X86_64_ACPI_H

#include <stdbool.h>
#include <stdint.h>

struct acpi_rsdp_v1 {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct acpi_rsdp_v2 {
    struct acpi_rsdp_v1 first;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t lapic_addr;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

/* MADT entry types (ACPI 6.x, table 5-45). */
#define ACPI_MADT_TYPE_LAPIC   0
#define ACPI_MADT_TYPE_IOAPIC  1
#define ACPI_MADT_TYPE_ISO     2
#define ACPI_MADT_TYPE_X2APIC  9

/* Flags shared by the type 0 and type 9 processor entries. An entry with
 * neither bit set describes a processor that can never be brought online and
 * must be ignored by OSPM. */
#define ACPI_MADT_CPU_ENABLED        0x00000001U
#define ACPI_MADT_CPU_ONLINE_CAPABLE 0x00000002U

struct acpi_madt_lapic {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_x2apic {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t acpi_processor_uid;
} __attribute__((packed));

struct acpi_madt_ioapic {
    uint8_t type;
    uint8_t length;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

struct acpi_madt_iso {
    uint8_t type;
    uint8_t length;
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

struct acpi_madt_ioapic_info {
    uint8_t id;
    uint32_t address;
    uint32_t gsi_base;
};

/* One processor as described by MADT, normalised across type 0 and type 9.
 * `apic_id` is 32-bit because x2APIC entries are not limited to 255. */
struct acpi_madt_cpu_info {
    uint32_t apic_id;
    uint32_t acpi_uid;
    uint32_t flags;
    bool x2apic;
};

struct acpi_madt_iso_info {
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
};

typedef int (*acpi_madt_iter_fn)(const struct acpi_madt_entry_header* entry, void* ctx);

int acpi_init(void);
bool acpi_is_available(void);
uint8_t acpi_revision(void);
uint64_t acpi_rsdp_physical(void);
const struct acpi_sdt_header* acpi_find_table(const char signature[4]);
const struct acpi_madt* acpi_get_madt(void);
int acpi_madt_foreach(acpi_madt_iter_fn fn, void* ctx);
int acpi_madt_get_lapic_addr(uint32_t* out_addr);
int acpi_madt_get_ioapic(uint32_t index, struct acpi_madt_ioapic_info* out_info);

/* Collect processor entries (MADT type 0 and type 9).
 *
 * Probe mode: with `out == NULL` or `max == 0`, `*out_count` receives the
 * total number of processor entries present and nothing is written.
 * Fill mode: `*out_count` receives the number of entries actually written,
 * which is capped at `max`; compare against a probe call to detect truncation.
 *
 * Entries are returned in MADT order and are not de-duplicated; firmware that
 * describes one processor twice is normalised by the caller. */
int acpi_madt_get_cpus(struct acpi_madt_cpu_info* out, uint32_t max, uint32_t* out_count);
int acpi_madt_get_iso_for_source(uint8_t source, struct acpi_madt_iso_info* out_info);

#endif /* _RODNIX_ARCH_X86_64_ACPI_H */
