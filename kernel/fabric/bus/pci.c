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
#include "../../include/console.h"
#include <stddef.h>
#include <stdint.h>

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

/* Enumerate PCI bus */
static void pci_enumerate(void)
{
    static fabric_device_t pci_devices[256];
    static pci_device_info_t pci_info[256];
    static uint32_t pci_device_count = 0;

    /* Enumerate bus 0, devices 0-31, functions 0-7 */
    for (uint8_t device = 0; device < 32; device++) {
        if (!pci_device_exists(0, device, 0)) {
            continue;
        }

        uint8_t header_type = pci_read_config8(0, device, 0, 0x0Eu);
        uint8_t max_functions = (header_type & 0x80u) ? 8u : 1u;

        for (uint8_t function = 0; function < max_functions; function++) {
            if (!pci_device_exists(0, device, function)) {
                continue;
            }
            
            /* Read device information */
            uint16_t vendor_id = pci_read_vendor(0, device, function);
            uint16_t device_id = pci_read_device_id(0, device, function);
            uint32_t class_reg = pci_read_class(0, device, function);
            uint8_t revision_id = pci_read_config8(0, device, function, 0x08u);
            uint16_t command = pci_read_config16(0, device, function, 0x04u);
            uint16_t status = pci_read_config16(0, device, function, 0x06u);
            header_type = pci_read_config8(0, device, function, 0x0Eu);
            
            uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFF);
            uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFF);
            uint8_t prog_if = (uint8_t)((class_reg >> 8) & 0xFF);
            
            if (pci_device_count >= 256) {
                break; /* Too many devices */
            }
            
            fabric_device_t *dev = &pci_devices[pci_device_count++];
            pci_device_info_t *info = &pci_info[pci_device_count - 1];
            info->bus = 0;
            info->device = device;
            info->function = function;
            info->revision_id = revision_id;
            info->header_type = header_type;
            info->command = command;
            info->status = status;
            for (uint8_t bar = 0; bar < PCI_BAR_COUNT; bar++) {
                info->bars[bar] = pci_read_config(
                    0, device, function, (uint8_t)(0x10u + (bar * 4u))
                );
            }
            
            /* Fill device structure */
            dev->name = "pci-device";
            dev->vendor_id = vendor_id;
            dev->device_id = device_id;
            dev->class_code = class_code;
            dev->subclass = subclass;
            dev->prog_if = prog_if;
            dev->bus_private = info;
            dev->driver_state = NULL;

            kprintf("[PCI] bdf=%u:%u.%u vendor=%x device=%x class=%x:%x:%x bar0=%x\n",
                    (unsigned)info->bus,
                    (unsigned)info->device,
                    (unsigned)info->function,
                    (unsigned)vendor_id,
                    (unsigned)device_id,
                    (unsigned)class_code,
                    (unsigned)subclass,
                    (unsigned)prog_if,
                    (unsigned)info->bars[0]);
            
            /* Publish device */
            fabric_device_publish(dev);
        }
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
