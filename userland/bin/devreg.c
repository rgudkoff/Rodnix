/*
 * devreg.c
 * Show Fabric devices as a device-registry tree.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "hwinfo.h"

#define FD_STDOUT 1
#define DEVREG_MAX_QUERY 64

static hwdev_info_t g_devs[DEVREG_MAX_QUERY];
static uint8_t g_seen[DEVREG_MAX_QUERY];

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

static const char* dispatch_state_name(uint8_t state)
{
    switch (state) {
        case 0: return "none";
        case 1: return "pending";
        case 2: return "matched";
        case 3: return "attached";
        case 4: return "failed";
        default: return "unknown";
    }
}

static const char* device_bus(const hwdev_info_t* dev)
{
    for (uint32_t i = 0; i < dev->property_count && i < HWINFO_PROP_MAX; i++) {
        if (dev->properties[i].type == HWINFO_PROP_STR &&
            dev->properties[i].key[0] &&
            dev->properties[i].str[0]) {
            const char* key = dev->properties[i].key;
            if (key[0] == 'b' && key[1] == 'u' && key[2] == 's' && key[3] == '\0') {
                return dev->properties[i].str;
            }
        }
    }
    return "unknown";
}

static void write_indent(uint32_t level)
{
    for (uint32_t i = 0; i < level; i++) {
        (void)write_str("  ");
    }
}

static void write_device_line(const hwdev_info_t* dev, uint32_t index)
{
    write_indent(1);
    (void)write_str("+- ");
    if (dev->is_pci) {
        write_u64((uint64_t)dev->pci_bus);
        (void)write_str(":");
        write_u64((uint64_t)dev->pci_device);
        (void)write_str(".");
        write_u64((uint64_t)dev->pci_function);
        (void)write_str(" ");
    }
    (void)write_str(dev->name[0] ? dev->name : "unnamed");
    (void)write_str(" [");
    (void)write_str(dispatch_state_name(dev->dispatch_state));
    (void)write_str("]");
    if (dev->driver[0]) {
        (void)write_str(" driver=");
        (void)write_str(dev->driver);
    }
    (void)write_str(" id=");
    write_hex_u16(dev->vendor_id);
    (void)write_str(":");
    write_hex_u16(dev->device_id);
    (void)write_str(" class=");
    write_hex_u8(dev->class_code);
    (void)write_str(":");
    write_hex_u8(dev->subclass);
    (void)write_str(":");
    write_hex_u8(dev->prog_if);
    (void)write_str(" #");
    write_u64((uint64_t)index);
    (void)write_str("\n");

    write_indent(2);
    (void)write_str("match attempts=");
    write_u64((uint64_t)dev->attach_attempts);
    (void)write_str(" attached=");
    write_u64((uint64_t)dev->attached);
    (void)write_str("\n");

    if (dev->is_pci) {
        write_indent(2);
        (void)write_str("pci irq=");
        write_u64((uint64_t)dev->pci_interrupt_line);
        (void)write_str("/");
        write_u64((uint64_t)dev->pci_interrupt_pin);
        (void)write_str(" cap=0x");
        write_hex_u16((uint16_t)(dev->pci_capability_bits >> 16));
        write_hex_u16((uint16_t)dev->pci_capability_bits);
        if (dev->pci_secondary_bus || dev->pci_subordinate_bus) {
            (void)write_str(" bridge=");
            write_u64((uint64_t)dev->pci_primary_bus);
            (void)write_str("->");
            write_u64((uint64_t)dev->pci_secondary_bus);
            (void)write_str("->");
            write_u64((uint64_t)dev->pci_subordinate_bus);
        }
        (void)write_str("\n");
    }

    for (uint32_t p = 0; p < dev->property_count && p < HWINFO_PROP_MAX; p++) {
        write_indent(2);
        (void)write_str("property ");
        (void)write_str(dev->properties[p].key);
        (void)write_str("=");
        if (dev->properties[p].type == HWINFO_PROP_U32) {
            (void)write_str("0x");
            write_hex_u16((uint16_t)(dev->properties[p].u32 >> 16));
            write_hex_u16((uint16_t)dev->properties[p].u32);
        } else if (dev->properties[p].type == HWINFO_PROP_STR) {
            (void)write_str("\"");
            (void)write_str(dev->properties[p].str);
            (void)write_str("\"");
        } else {
            (void)write_str("?");
        }
        (void)write_str("\n");
    }
}

int main(void)
{
    uint32_t total = 0;
    long n = posix_hwlist(g_devs, DEVREG_MAX_QUERY, &total);
    if (n < 0) {
        (void)write_str("devreg: syscall failed\n");
        return 1;
    }

    for (long i = 0; i < n; i++) {
        g_seen[i] = 0;
    }

    (void)write_str("+- Device Registry root\n");

    for (long i = 0; i < n; i++) {
        const char* bus = device_bus(&g_devs[i]);
        uint8_t bus_seen = 0;
        for (long j = 0; j < i; j++) {
            if (!g_seen[j]) {
                continue;
            }
            const char* prev_bus = device_bus(&g_devs[j]);
            uint32_t k = 0;
            while (bus[k] && prev_bus[k] && bus[k] == prev_bus[k]) {
                k++;
            }
            if (bus[k] == '\0' && prev_bus[k] == '\0') {
                bus_seen = 1;
                break;
            }
        }
        if (bus_seen) {
            continue;
        }

        g_seen[i] = 1;
        write_indent(1);
        (void)write_str("+- ");
        (void)write_str(bus);
        (void)write_str("-bus\n");
        write_device_line(&g_devs[i], (uint32_t)i);

        for (long j = i + 1; j < n; j++) {
            const char* other_bus = device_bus(&g_devs[j]);
            uint32_t k = 0;
            while (bus[k] && other_bus[k] && bus[k] == other_bus[k]) {
                k++;
            }
            if (bus[k] == '\0' && other_bus[k] == '\0') {
                g_seen[j] = 1;
                write_device_line(&g_devs[j], (uint32_t)j);
            }
        }
    }

    if (total > (uint32_t)n) {
        (void)write_str("devreg: output truncated\n");
    }
    return 0;
}
