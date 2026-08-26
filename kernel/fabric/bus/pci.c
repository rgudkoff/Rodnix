/**
 * @file pci.c
 * @brief PCI bus implementation
 * 
 * Minimal PCI enumeration through config space.
 */

#include "pci.h"
#include "bus.h"
#include "../fabric.h"
#include "../device/device.h"
#include "../../../include/dev/pci/pcireg.h"
#include "../../../include/common.h"
#include "../../core/interrupts.h"
#include "../../arch/x86_64/apic.h"
#include "../../arch/x86_64/lapic_access.h"
#include "../../arch/x86_64/pic.h"
#include "../../../include/error.h"
#include "../../include/console.h"
#include <stddef.h>
#include <stdint.h>

#define PCI_MAX_BUSES 256u
#define PCI_MAX_DEVICES 256u

/* PCI Configuration Space Address */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* Read 32-bit value from PCI config space */
static uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t address = (1UL << 31) | 
                       ((uint32_t)bus << 16) | 
                       ((uint32_t)device << 11) | 
                       ((uint32_t)function << 8) | 
                       (offset & 0xFC);
    
    __asm__ volatile ("outl %%eax, %0" : : "Nd"((uint16_t)PCI_CONFIG_ADDRESS), "a"(address));
    uint32_t value;
    __asm__ volatile ("inl %1, %%eax" : "=a"(value) : "Nd"((uint16_t)PCI_CONFIG_DATA));
    
    return value;
}

static uint16_t pci_read_config16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t v = pci_read_config(bus, device, function, (uint8_t)(offset & 0xFCu));
    uint8_t shift = (uint8_t)((offset & 0x2u) * 8u);
    return (uint16_t)((v >> shift) & 0xFFFFu);
}

static uint8_t pci_read_config8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t v = pci_read_config(bus, device, function, (uint8_t)(offset & 0xFCu));
    uint8_t shift = (uint8_t)((offset & 0x3u) * 8u);
    return (uint8_t)((v >> shift) & 0xFFu);
}

static void pci_write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    uint32_t address = (1UL << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)function << 8) |
                       (offset & 0xFCu);

    __asm__ volatile ("outl %%eax, %0" : : "Nd"((uint16_t)PCI_CONFIG_ADDRESS), "a"(address));
    __asm__ volatile ("outl %%eax, %0" : : "Nd"((uint16_t)PCI_CONFIG_DATA), "a"(value));
}

static void pci_write_config16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value)
{
    uint32_t cur = pci_read_config(bus, device, function, (uint8_t)(offset & 0xFCu));
    uint8_t shift = (uint8_t)((offset & 0x2u) * 8u);
    cur &= ~(0xFFFFu << shift);
    cur |= ((uint32_t)value) << shift;
    pci_write_config(bus, device, function, (uint8_t)(offset & 0xFCu), cur);
}

static void pci_write_config32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    pci_write_config(bus, device, function, (uint8_t)(offset & 0xFCu), value);
}

static void pci_scan_capabilities(uint8_t bus,
                                  uint8_t device,
                                  uint8_t function,
                                  uint16_t status,
                                  uint8_t header_type,
                                  pci_device_info_t* info)
{
    uint8_t cap_ptr = 0;
    uint8_t normalized_header = (uint8_t)(header_type & 0x7Fu);

    if (!info || (status & PCI_STATUS_CAP_LIST) == 0) {
        return;
    }

    if (normalized_header == 0x00u || normalized_header == 0x01u) {
        cap_ptr = pci_read_config8(bus, device, function, PCIR_CAP_PTR);
    } else if (normalized_header == 0x02u) {
        cap_ptr = pci_read_config8(bus, device, function, 0x14u);
    } else {
        return;
    }

    info->cap_ptr = (uint8_t)(cap_ptr & 0xFCu);
    cap_ptr = info->cap_ptr;
    for (uint32_t i = 0; i < 48u && cap_ptr >= 0x40u; i++) {
        uint8_t cap_id = pci_read_config8(bus, device, function, cap_ptr + 0u);
        uint8_t next = (uint8_t)(pci_read_config8(bus, device, function, cap_ptr + 1u) & 0xFCu);
        if (cap_id < 32u) {
            info->capability_bits |= (1u << cap_id);
        }
        info->cap_count++;
        if (next == 0u || next == cap_ptr) {
            break;
        }
        cap_ptr = next;
    }
}

void pci_command_set(pci_device_info_t* info, uint16_t bits)
{
    if (!info) {
        return;
    }
    uint16_t cmd = pci_read_config16(info->bus, info->device, info->function, PCIR_COMMAND);
    cmd |= bits;
    pci_write_config16(info->bus, info->device, info->function, PCIR_COMMAND, cmd);
    info->command = cmd;
}

int pci_find_capability(fabric_device_t* dev, uint8_t cap_id, uint8_t* out_offset)
{
    pci_device_info_t* info;
    uint8_t off;

    if (!dev || !dev->bus_private || !out_offset) {
        return RDNX_E_INVALID;
    }

    info = (pci_device_info_t*)dev->bus_private;
    if ((info->status & PCI_STATUS_CAP_LIST) == 0 || info->cap_ptr < 0x40u) {
        return RDNX_E_NOTFOUND;
    }

    off = info->cap_ptr;
    for (uint32_t i = 0; i < 48u && off >= 0x40u; i++) {
        uint8_t id = pci_read_config8(info->bus, info->device, info->function, off + 0u);
        uint8_t next = (uint8_t)(pci_read_config8(info->bus, info->device, info->function, off + 1u) & 0xFCu);
        if (id == cap_id) {
            *out_offset = off;
            return RDNX_OK;
        }
        if (next == 0u || next == off) {
            break;
        }
        off = next;
    }

    return RDNX_E_NOTFOUND;
}

bool pci_has_capability(fabric_device_t* dev, uint8_t cap_id)
{
    pci_device_info_t* info;

    if (!dev || !dev->bus_private) {
        return false;
    }
    info = (pci_device_info_t*)dev->bus_private;
    if (cap_id < 32u && (info->capability_bits & (1u << cap_id)) != 0u) {
        return true;
    }
    return false;
}

int pci_irq_vector(fabric_device_t* dev)
{
    pci_device_info_t* info;

    if (!dev || !dev->bus_private) {
        return RDNX_E_INVALID;
    }
    info = (pci_device_info_t*)dev->bus_private;
    if (info->interrupt_line == 0u || info->interrupt_line == 0xFFu || info->interrupt_line > 15u) {
        return RDNX_E_NOTFOUND;
    }
    return 32 + (int)info->interrupt_line;
}

int pci_enable_msi(fabric_device_t* dev, uint8_t vector)
{
    pci_device_info_t* info;
    uint8_t cap_off = 0;
    uint16_t msi_ctrl;
    uint16_t command;
    uint32_t msg_addr_lo;
    uint16_t msg_data;

    if (!dev || !dev->bus_private) {
        return RDNX_E_INVALID;
    }
    if (vector < 32u || vector == 128u) {
        return RDNX_E_UNSUPPORTED;
    }
    if (!apic_is_available() || !lapic_access_ready()) {
        return RDNX_E_UNSUPPORTED;
    }

    info = (pci_device_info_t*)dev->bus_private;
    if (pci_find_capability(dev, PCIY_MSI, &cap_off) != RDNX_OK) {
        return RDNX_E_NOTFOUND;
    }

    msi_ctrl = pci_read_config16(info->bus, info->device, info->function, (uint8_t)(cap_off + PCIR_MSI_CTRL));
    msg_addr_lo = 0xFEE00000u | ((uint32_t)apic_get_lapic_id() << 12);
    msg_data = vector;

    pci_write_config32(info->bus, info->device, info->function, (uint8_t)(cap_off + PCIR_MSI_ADDR), msg_addr_lo);
    if ((msi_ctrl & PCIM_MSICTRL_64BIT) != 0u) {
        pci_write_config32(info->bus, info->device, info->function, (uint8_t)(cap_off + PCIR_MSI_ADDR_HIGH), 0u);
        pci_write_config16(info->bus, info->device, info->function, (uint8_t)(cap_off + PCIR_MSI_DATA_64), msg_data);
    } else {
        pci_write_config16(info->bus, info->device, info->function, (uint8_t)(cap_off + PCIR_MSI_DATA_32), msg_data);
    }

    msi_ctrl |= PCIM_MSICTRL_MSI_ENABLE;
    pci_write_config16(info->bus, info->device, info->function, (uint8_t)(cap_off + PCIR_MSI_CTRL), msi_ctrl);

    command = pci_read_config16(info->bus, info->device, info->function, PCIR_COMMAND);
    command |= PCIM_CMD_INTxDIS;
    pci_write_config16(info->bus, info->device, info->function, PCIR_COMMAND, command);
    info->command = command;
    return RDNX_OK;
}

int pci_disable_msi(fabric_device_t* dev)
{
    pci_device_info_t* info;
    uint8_t cap_off = 0;
    uint16_t msi_ctrl;
    uint16_t command;

    if (!dev || !dev->bus_private) {
        return RDNX_E_INVALID;
    }

    info = (pci_device_info_t*)dev->bus_private;
    if (pci_find_capability(dev, PCIY_MSI, &cap_off) != RDNX_OK) {
        return RDNX_E_NOTFOUND;
    }

    msi_ctrl = pci_read_config16(info->bus, info->device, info->function, (uint8_t)(cap_off + PCIR_MSI_CTRL));
    msi_ctrl &= (uint16_t)~PCIM_MSICTRL_MSI_ENABLE;
    pci_write_config16(info->bus, info->device, info->function, (uint8_t)(cap_off + PCIR_MSI_CTRL), msi_ctrl);

    command = pci_read_config16(info->bus, info->device, info->function, PCIR_COMMAND);
    command &= (uint16_t)~PCIM_CMD_INTxDIS;
    pci_write_config16(info->bus, info->device, info->function, PCIR_COMMAND, command);
    info->command = command;
    return RDNX_OK;
}

int pci_irq_bind(fabric_device_t* dev,
                 fabric_irq_handler_t handler,
                 void* arg,
                 pci_irq_binding_t* binding)
{
    int vector;
    int rc;
    int allocated_vector = -1;
    pci_irq_binding_t local_binding;

    if (!dev || !handler || !binding) {
        return RDNX_E_INVALID;
    }

    memset(&local_binding, 0, sizeof(local_binding));
    local_binding.vector = -1;

    vector = pci_irq_vector(dev);
    if (pci_has_capability(dev, PCIY_MSI)) {
        allocated_vector = interrupt_vector_alloc(64u, 223u);
        if (allocated_vector >= 0) {
            rc = pci_enable_msi(dev, (uint8_t)allocated_vector);
        } else {
            rc = RDNX_E_BUSY;
        }
        if (rc == RDNX_OK) {
            vector = allocated_vector;
            local_binding.mode = PCI_IRQ_MODE_MSI;
            local_binding.vector_allocated = 1u;
        } else if (allocated_vector >= 0) {
            interrupt_vector_free((uint32_t)allocated_vector);
        }
    }

    if (vector < 0) {
        return RDNX_E_NOTFOUND;
    }

    rc = fabric_request_irq(vector, handler, arg);
    if (rc != RDNX_OK) {
        if (local_binding.mode == PCI_IRQ_MODE_MSI) {
            (void)pci_disable_msi(dev);
            if (local_binding.vector_allocated) {
                interrupt_vector_free((uint32_t)vector);
            }
        }
        return rc;
    }

    local_binding.vector = vector;
    if (local_binding.mode != PCI_IRQ_MODE_MSI) {
        local_binding.mode = PCI_IRQ_MODE_INTX;
        if (vector >= 32 && vector < 48) {
            uint8_t irq = (uint8_t)(vector - 32);
            if (apic_is_available() && ioapic_is_available()) {
                apic_enable_irq(irq);
            } else {
                pic_enable_irq(irq);
            }
        }
    }

    *binding = local_binding;
    return RDNX_OK;
}

void pci_irq_unbind(fabric_device_t* dev,
                    fabric_irq_handler_t handler,
                    const pci_irq_binding_t* binding)
{
    if (!dev || !handler || !binding || binding->vector < 0) {
        return;
    }

    fabric_free_irq(binding->vector, handler);

    if (binding->mode == PCI_IRQ_MODE_MSI) {
        (void)pci_disable_msi(dev);
        if (binding->vector_allocated) {
            interrupt_vector_free((uint32_t)binding->vector);
        }
        return;
    }

    if (binding->mode == PCI_IRQ_MODE_INTX &&
        binding->vector >= 32 && binding->vector < 48) {
        uint8_t irq = (uint8_t)(binding->vector - 32);
        if (apic_is_available() && ioapic_is_available()) {
            apic_disable_irq(irq);
        } else {
            pic_disable_irq(irq);
        }
    }
}

/* Read vendor ID and device ID */
static uint16_t pci_read_vendor(uint8_t bus, uint8_t device, uint8_t function)
{
    return (uint16_t)(pci_read_config(bus, device, function, 0) & 0xFFFF);
}

static uint16_t pci_read_device_id(uint8_t bus, uint8_t device, uint8_t function)
{
    return (uint16_t)((pci_read_config(bus, device, function, 0) >> 16) & 0xFFFF);
}

/* Read class code */
static uint32_t pci_read_class(uint8_t bus, uint8_t device, uint8_t function)
{
    return pci_read_config(bus, device, function, 8);
}

/* Check if device exists */
static bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function)
{
    uint16_t vendor = pci_read_vendor(bus, device, function);
    return vendor != 0xFFFF && vendor != 0x0000;
}

static void pci_publish_function(uint8_t bus,
                                 uint8_t device,
                                 uint8_t function,
                                 fabric_device_t* pci_devices,
                                 pci_device_info_t* pci_info,
                                 uint32_t* pci_device_count,
                                 uint8_t* seen_buses)
{
    uint16_t vendor_id = pci_read_vendor(bus, device, function);
    uint16_t device_id = pci_read_device_id(bus, device, function);
    uint32_t class_reg = pci_read_class(bus, device, function);
    uint8_t revision_id = pci_read_config8(bus, device, function, 0x08u);
    uint16_t command = pci_read_config16(bus, device, function, 0x04u);
    uint16_t status = pci_read_config16(bus, device, function, 0x06u);
    uint8_t header_type = pci_read_config8(bus, device, function, 0x0Eu);
    uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
    uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
    uint8_t prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);
    uint32_t slot = *pci_device_count;

    if (slot >= PCI_MAX_DEVICES) {
        return;
    }

    fabric_device_t* dev = &pci_devices[slot];
    pci_device_info_t* info = &pci_info[slot];
    memset(info, 0, sizeof(*info));
    info->bus = bus;
    info->device = device;
    info->function = function;
    info->revision_id = revision_id;
    info->header_type = header_type;
    info->command = command;
    info->status = status;
    info->interrupt_line = pci_read_config8(bus, device, function, 0x3Cu);
    info->interrupt_pin = pci_read_config8(bus, device, function, 0x3Du);
    if ((header_type & 0x7Fu) == 0x01u) {
        info->primary_bus = pci_read_config8(bus, device, function, 0x18u);
        info->secondary_bus = pci_read_config8(bus, device, function, 0x19u);
        info->subordinate_bus = pci_read_config8(bus, device, function, 0x1Au);
    }
    pci_scan_capabilities(bus, device, function, status, header_type, info);
    for (uint8_t bar = 0; bar < PCI_BAR_COUNT; bar++) {
        info->bars[bar] = pci_read_config(bus, device, function, (uint8_t)(0x10u + (bar * 4u)));
    }

    dev->name = "pci-device";
    dev->vendor_id = vendor_id;
    dev->device_id = device_id;
    dev->class_code = class_code;
    dev->subclass = subclass;
    dev->prog_if = prog_if;
    dev->bus_private = info;
    dev->driver_state = NULL;

    (*pci_device_count)++;

    kprintf("[PCI] bdf=%u:%u.%u vendor=%x device=%x class=%x:%x:%x cap=%x sec=%u sub=%u bar0=%x\n",
            (unsigned)info->bus,
            (unsigned)info->device,
            (unsigned)info->function,
            (unsigned)vendor_id,
            (unsigned)device_id,
            (unsigned)class_code,
            (unsigned)subclass,
            (unsigned)prog_if,
            (unsigned)info->capability_bits,
            (unsigned)info->secondary_bus,
            (unsigned)info->subordinate_bus,
            (unsigned)info->bars[0]);

    fabric_device_publish(dev);

    if (class_code == PCIC_BRIDGE && subclass == PCIS_BRIDGE_PCI) {
        uint8_t secondary = info->secondary_bus;
        uint8_t subordinate = info->subordinate_bus;
        if (secondary != 0u && subordinate >= secondary) {
            for (uint16_t child_bus = secondary; child_bus <= subordinate && child_bus < PCI_MAX_BUSES; child_bus++) {
                if (seen_buses[child_bus]) {
                    continue;
                }
                seen_buses[child_bus] = 2u;
            }
        }
    }
}

static void pci_enumerate_bus(uint8_t bus,
                              fabric_device_t* pci_devices,
                              pci_device_info_t* pci_info,
                              uint32_t* pci_device_count,
                              uint8_t* seen_buses)
{
    if (seen_buses[bus] == 1u) {
        return;
    }
    seen_buses[bus] = 1u;

    for (uint8_t device = 0; device < 32; device++) {
        uint8_t header_type;
        uint8_t max_functions;

        if (!pci_device_exists(bus, device, 0)) {
            continue;
        }

        header_type = pci_read_config8(bus, device, 0, 0x0Eu);
        max_functions = (header_type & 0x80u) ? 8u : 1u;

        for (uint8_t function = 0; function < max_functions; function++) {
            if (!pci_device_exists(bus, device, function)) {
                continue;
            }
            pci_publish_function(bus, device, function, pci_devices, pci_info, pci_device_count, seen_buses);
        }
    }
}

/* Enumerate PCI bus */
static void pci_enumerate(void)
{
    static fabric_device_t pci_devices[PCI_MAX_DEVICES];
    static pci_device_info_t pci_info[PCI_MAX_DEVICES];
    static uint8_t seen_buses[PCI_MAX_BUSES];
    uint32_t pci_device_count = 0;

    memset(pci_devices, 0, sizeof(pci_devices));
    memset(pci_info, 0, sizeof(pci_info));
    memset(seen_buses, 0, sizeof(seen_buses));

    seen_buses[0] = 2u;
    for (uint16_t bus = 0; bus < PCI_MAX_BUSES; bus++) {
        if (seen_buses[bus] == 0u) {
            continue;
        }
        pci_enumerate_bus((uint8_t)bus, pci_devices, pci_info, &pci_device_count, seen_buses);
    }
}

static fabric_bus_t pci_bus = {
    .name = "pci",
    .enumerate = pci_enumerate,
    .rescan = NULL
};

void pci_bus_init(void)
{
    fabric_bus_register(&pci_bus);
}
