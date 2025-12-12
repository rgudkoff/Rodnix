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

/* PCI Device structure for bus_private */
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
} pci_device_info_t;

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
    /* Enumerate bus 0, devices 0-31, functions 0-7 */
    for (uint8_t device = 0; device < 32; device++) {
        for (uint8_t function = 0; function < 8; function++) {
            if (!pci_device_exists(0, device, function)) {
                if (function == 0) {
                    break; /* No device at this slot */
                }
                continue;
            }
            
            /* Read device information */
            uint16_t vendor_id = pci_read_vendor(0, device, function);
            uint16_t device_id = pci_read_device_id(0, device, function);
            uint32_t class_reg = pci_read_class(0, device, function);
            
            uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFF);
            uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFF);
            uint8_t prog_if = (uint8_t)((class_reg >> 8) & 0xFF);
            
            /* Allocate device structure */
            static fabric_device_t pci_devices[256];
            static uint32_t pci_device_count = 0;
            
            if (pci_device_count >= 256) {
                break; /* Too many devices */
            }
            
            fabric_device_t *dev = &pci_devices[pci_device_count++];
            
            /* Allocate PCI device info */
            static pci_device_info_t pci_info[256];
            pci_info[pci_device_count - 1].bus = 0;
            pci_info[pci_device_count - 1].device = device;
            pci_info[pci_device_count - 1].function = function;
            
            /* Fill device structure */
            dev->name = "pci-device";
            dev->vendor_id = vendor_id;
            dev->device_id = device_id;
            dev->class_code = class_code;
            dev->subclass = subclass;
            dev->prog_if = prog_if;
            dev->bus_private = &pci_info[pci_device_count - 1];
            dev->driver_state = NULL;
            
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

