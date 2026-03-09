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
int acpi_madt_get_iso_for_source(uint8_t source, struct acpi_madt_iso_info* out_info);

#endif /* _RODNIX_ARCH_X86_64_ACPI_H */
