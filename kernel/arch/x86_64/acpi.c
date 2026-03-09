/**
 * @file acpi.c
 * @brief Basic ACPI discovery and SDT lookup for x86_64.
 */

#include "acpi.h"
#include "config.h"
#include "../../../include/common.h"
#include "../../../include/console.h"

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_RSDT_SIGNATURE "RSDT"
#define ACPI_XSDT_SIGNATURE "XSDT"

#define ACPI_RSDP_SCAN_STEP 16ULL
#define ACPI_EBDA_PTR_PHYS  0x40EULL
#define ACPI_EBDA_SCAN_SIZE 1024ULL
#define ACPI_BIOS_SCAN_START 0xE0000ULL
#define ACPI_BIOS_SCAN_END   0x100000ULL

struct acpi_root_state {
    bool initialized;
    bool available;
    uint8_t revision;
    uint64_t rsdp_phys;
    uint64_t rsdt_phys;
    uint64_t xsdt_phys;
};

static struct acpi_root_state g_acpi;

static bool acpi_checksum_ok(const void* ptr, size_t len)
{
    if (!ptr || len == 0) {
        return false;
    }

    const uint8_t* p = (const uint8_t*)ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum == 0;
}

static uint64_t acpi_scan_rsdp_range(uint64_t start_phys, uint64_t end_phys)
{
    if (end_phys <= start_phys) {
        return 0;
    }

    for (uint64_t addr = start_phys; addr + sizeof(struct acpi_rsdp_v1) <= end_phys;
         addr += ACPI_RSDP_SCAN_STEP) {
        const struct acpi_rsdp_v1* rsdp =
            (const struct acpi_rsdp_v1*)X86_64_PHYS_TO_VIRT(addr);

        if (memcmp(rsdp->signature, ACPI_RSDP_SIGNATURE, 8) != 0) {
            continue;
        }

        if (!acpi_checksum_ok(rsdp, sizeof(struct acpi_rsdp_v1))) {
            continue;
        }

        if (rsdp->revision >= 2) {
            const struct acpi_rsdp_v2* rsdp2 =
                (const struct acpi_rsdp_v2*)rsdp;
            uint32_t len = rsdp2->length;
            if (len < sizeof(struct acpi_rsdp_v2) || len > 4096U) {
                continue;
            }
            if (!acpi_checksum_ok(rsdp2, len)) {
                continue;
            }
        }

        return addr;
    }

    return 0;
}

static bool acpi_sdt_header_valid(uint64_t phys,
                                  const char expected_sig[4],
                                  struct acpi_sdt_header** out_hdr)
{
    struct acpi_sdt_header* hdr =
        (struct acpi_sdt_header*)X86_64_PHYS_TO_VIRT(phys);

    if (memcmp(hdr->signature, expected_sig, 4) != 0) {
        return false;
    }

    if (hdr->length < sizeof(struct acpi_sdt_header) || hdr->length > (1024U * 1024U)) {
        return false;
    }

    if (!acpi_checksum_ok(hdr, hdr->length)) {
        return false;
    }

    if (out_hdr) {
        *out_hdr = hdr;
    }
    return true;
}

int acpi_init(void)
{
    if (g_acpi.initialized) {
        return g_acpi.available ? 0 : -1;
    }

    memset(&g_acpi, 0, sizeof(g_acpi));

    uint64_t rsdp_phys = 0;

    uint16_t ebda_segment =
        *(volatile uint16_t*)X86_64_PHYS_TO_VIRT(ACPI_EBDA_PTR_PHYS);
    if (ebda_segment != 0) {
        uint64_t ebda_phys = (uint64_t)ebda_segment << 4;
        rsdp_phys = acpi_scan_rsdp_range(ebda_phys, ebda_phys + ACPI_EBDA_SCAN_SIZE);
    }

    if (rsdp_phys == 0) {
        rsdp_phys = acpi_scan_rsdp_range(ACPI_BIOS_SCAN_START, ACPI_BIOS_SCAN_END);
    }

    if (rsdp_phys == 0) {
        g_acpi.initialized = true;
        g_acpi.available = false;
        kputs("[ACPI] RSDP not found\n");
        return -1;
    }

    const struct acpi_rsdp_v1* rsdp =
        (const struct acpi_rsdp_v1*)X86_64_PHYS_TO_VIRT(rsdp_phys);

    g_acpi.rsdp_phys = rsdp_phys;
    g_acpi.revision = rsdp->revision;
    g_acpi.rsdt_phys = rsdp->rsdt_address;

    if (rsdp->revision >= 2) {
        const struct acpi_rsdp_v2* rsdp2 = (const struct acpi_rsdp_v2*)rsdp;
        g_acpi.xsdt_phys = rsdp2->xsdt_address;
    }

    bool root_ok = false;
    struct acpi_sdt_header* root = NULL;

    if (g_acpi.xsdt_phys != 0) {
        root_ok = acpi_sdt_header_valid(g_acpi.xsdt_phys, ACPI_XSDT_SIGNATURE, &root);
    }

    if (!root_ok && g_acpi.rsdt_phys != 0) {
        root_ok = acpi_sdt_header_valid(g_acpi.rsdt_phys, ACPI_RSDT_SIGNATURE, &root);
        if (root_ok) {
            g_acpi.xsdt_phys = 0;
        }
    }

    g_acpi.initialized = true;
    g_acpi.available = root_ok;

    if (!root_ok) {
        kputs("[ACPI] Root SDT invalid\n");
        return -1;
    }

    if (g_acpi.xsdt_phys != 0) {
        kprintf("[ACPI] XSDT ready rev=%u rsdp=%llx xsdt=%llx\n",
                (unsigned)g_acpi.revision,
                (unsigned long long)g_acpi.rsdp_phys,
                (unsigned long long)g_acpi.xsdt_phys);
    } else {
        kprintf("[ACPI] RSDT ready rev=%u rsdp=%llx rsdt=%llx\n",
                (unsigned)g_acpi.revision,
                (unsigned long long)g_acpi.rsdp_phys,
                (unsigned long long)g_acpi.rsdt_phys);
    }

    return 0;
}

bool acpi_is_available(void)
{
    return g_acpi.available;
}

uint8_t acpi_revision(void)
{
    return g_acpi.revision;
}

uint64_t acpi_rsdp_physical(void)
{
    return g_acpi.rsdp_phys;
}

const struct acpi_sdt_header* acpi_find_table(const char signature[4])
{
    if (!signature) {
        return NULL;
    }
    if (!g_acpi.initialized || !g_acpi.available) {
        return NULL;
    }

    uint64_t root_phys = g_acpi.xsdt_phys != 0 ? g_acpi.xsdt_phys : g_acpi.rsdt_phys;
    struct acpi_sdt_header* root =
        (struct acpi_sdt_header*)X86_64_PHYS_TO_VIRT(root_phys);
    uint32_t root_len = root->length;

    if (root_len < sizeof(struct acpi_sdt_header)) {
        return NULL;
    }

    bool is_xsdt = (g_acpi.xsdt_phys != 0);
    uint32_t entry_size = is_xsdt ? 8U : 4U;
    uint32_t payload = root_len - (uint32_t)sizeof(struct acpi_sdt_header);
    uint32_t count = payload / entry_size;
    if (count > 1024U) {
        count = 1024U;
    }

    const uint8_t* entries = ((const uint8_t*)root) + sizeof(struct acpi_sdt_header);
    for (uint32_t i = 0; i < count; i++) {
        uint64_t table_phys;
        if (is_xsdt) {
            table_phys = ((const uint64_t*)entries)[i];
        } else {
            table_phys = ((const uint32_t*)entries)[i];
        }
        if (table_phys == 0) {
            continue;
        }

        struct acpi_sdt_header* hdr =
            (struct acpi_sdt_header*)X86_64_PHYS_TO_VIRT(table_phys);
        if (memcmp(hdr->signature, signature, 4) != 0) {
            continue;
        }
        if (hdr->length < sizeof(struct acpi_sdt_header) || hdr->length > (1024U * 1024U)) {
            continue;
        }
        if (!acpi_checksum_ok(hdr, hdr->length)) {
            continue;
        }

        return hdr;
    }

    return NULL;
}

const struct acpi_madt* acpi_get_madt(void)
{
    const struct acpi_sdt_header* madt_hdr = acpi_find_table("APIC");
    if (!madt_hdr) {
        return NULL;
    }

    const struct acpi_madt* madt = (const struct acpi_madt*)madt_hdr;
    if (madt->header.length < sizeof(struct acpi_madt)) {
        return NULL;
    }
    return madt;
}

int acpi_madt_foreach(acpi_madt_iter_fn fn, void* ctx)
{
    if (!fn) {
        return -1;
    }

    const struct acpi_madt* madt = acpi_get_madt();
    if (!madt) {
        return -1;
    }

    const uint8_t* madt_data = (const uint8_t*)madt;
    uint32_t madt_length = madt->header.length;
    uint32_t offset = sizeof(struct acpi_madt);

    while (offset + sizeof(struct acpi_madt_entry_header) <= madt_length) {
        const struct acpi_madt_entry_header* entry =
            (const struct acpi_madt_entry_header*)(madt_data + offset);

        if (entry->length < sizeof(struct acpi_madt_entry_header)) {
            return -1;
        }
        if (offset + entry->length > madt_length) {
            return -1;
        }

        int rc = fn(entry, ctx);
        if (rc != 0) {
            return rc;
        }

        offset += entry->length;
    }

    return 0;
}

int acpi_madt_get_lapic_addr(uint32_t* out_addr)
{
    if (!out_addr) {
        return -1;
    }

    const struct acpi_madt* madt = acpi_get_madt();
    if (!madt) {
        return -1;
    }

    *out_addr = madt->lapic_addr;
    return 0;
}

int acpi_madt_get_ioapic(uint32_t index, struct acpi_madt_ioapic_info* out_info)
{
    if (!out_info) {
        return -1;
    }

    const struct acpi_madt* madt = acpi_get_madt();
    if (!madt) {
        return -1;
    }

    const uint8_t* madt_data = (const uint8_t*)madt;
    uint32_t madt_length = madt->header.length;
    uint32_t offset = sizeof(struct acpi_madt);
    uint32_t ioapic_idx = 0;

    while (offset + sizeof(struct acpi_madt_entry_header) <= madt_length) {
        const struct acpi_madt_entry_header* entry =
            (const struct acpi_madt_entry_header*)(madt_data + offset);

        if (entry->length < sizeof(struct acpi_madt_entry_header)) {
            return -1;
        }
        if (offset + entry->length > madt_length) {
            return -1;
        }

        if (entry->type == 1 && entry->length >= sizeof(struct acpi_madt_ioapic)) {
            if (ioapic_idx == index) {
                const struct acpi_madt_ioapic* ioapic =
                    (const struct acpi_madt_ioapic*)entry;
                out_info->id = ioapic->ioapic_id;
                out_info->address = ioapic->ioapic_addr;
                out_info->gsi_base = ioapic->gsi_base;
                return 0;
            }
            ioapic_idx++;
        }

        offset += entry->length;
    }

    return -1;
}

int acpi_madt_get_iso_for_source(uint8_t source, struct acpi_madt_iso_info* out_info)
{
    if (!out_info) {
        return -1;
    }

    const struct acpi_madt* madt = acpi_get_madt();
    if (!madt) {
        return -1;
    }

    const uint8_t* madt_data = (const uint8_t*)madt;
    uint32_t madt_length = madt->header.length;
    uint32_t offset = sizeof(struct acpi_madt);

    while (offset + sizeof(struct acpi_madt_entry_header) <= madt_length) {
        const struct acpi_madt_entry_header* entry =
            (const struct acpi_madt_entry_header*)(madt_data + offset);

        if (entry->length < sizeof(struct acpi_madt_entry_header)) {
            return -1;
        }
        if (offset + entry->length > madt_length) {
            return -1;
        }

        if (entry->type == 2 && entry->length >= sizeof(struct acpi_madt_iso)) {
            const struct acpi_madt_iso* iso = (const struct acpi_madt_iso*)entry;
            if (iso->source == source) {
                out_info->bus = iso->bus;
                out_info->source = iso->source;
                out_info->gsi = iso->gsi;
                out_info->flags = iso->flags;
                return 0;
            }
        }

        offset += entry->length;
    }

    return -1;
}
