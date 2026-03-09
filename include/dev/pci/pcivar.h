#ifndef _RODNIX_COMPAT_DEV_PCI_PCIVAR_H
#define _RODNIX_COMPAT_DEV_PCI_PCIVAR_H

#include <stdint.h>
#include "../../sys/bus.h"
#include "../../../kernel/fabric/bus/pci.h"
#include "pcireg.h"

#define RODNIX_PCI_CFG_ADDR 0xCF8u
#define RODNIX_PCI_CFG_DATA 0xCFCu

static inline const pci_device_info_t* pci_get_info(device_t dev)
{
    if (!dev || !dev->bus_private) {
        return (const pci_device_info_t*)0;
    }
    return (const pci_device_info_t*)dev->bus_private;
}

static inline uint16_t pci_get_vendor(device_t dev)
{
    return dev ? dev->vendor_id : 0xFFFFu;
}

static inline uint16_t pci_get_device(device_t dev)
{
    return dev ? dev->device_id : 0xFFFFu;
}

static inline uint8_t pci_get_class(device_t dev)
{
    return dev ? dev->class_code : 0;
}

static inline uint8_t pci_get_subclass(device_t dev)
{
    return dev ? dev->subclass : 0;
}

static inline uint8_t pci_get_progif(device_t dev)
{
    return dev ? dev->prog_if : 0;
}

static inline uint8_t pci_get_bus(device_t dev)
{
    const pci_device_info_t* p = pci_get_info(dev);
    return p ? p->bus : 0;
}

static inline uint8_t pci_get_slot(device_t dev)
{
    const pci_device_info_t* p = pci_get_info(dev);
    return p ? p->device : 0;
}

static inline uint8_t pci_get_function(device_t dev)
{
    const pci_device_info_t* p = pci_get_info(dev);
    return p ? p->function : 0;
}

static inline uint32_t pci_read_bar(device_t dev, uint32_t bar)
{
    const pci_device_info_t* p = pci_get_info(dev);
    if (!p || bar >= PCI_BAR_COUNT) {
        return 0;
    }
    return p->bars[bar];
}

static inline uint32_t pci_cfg_addr(device_t dev, uint32_t reg)
{
    return (1u << 31) |
           ((uint32_t)pci_get_bus(dev) << 16) |
           ((uint32_t)pci_get_slot(dev) << 11) |
           ((uint32_t)pci_get_function(dev) << 8) |
           (reg & 0xFCu);
}

static inline uint32_t pci_read_config(device_t dev, uint32_t reg, uint32_t width)
{
    uint32_t addr = pci_cfg_addr(dev, reg);
    uint32_t value = 0;
    __asm__ volatile ("outl %%eax, %0" : : "Nd"((uint16_t)RODNIX_PCI_CFG_ADDR), "a"(addr));
    __asm__ volatile ("inl %1, %%eax" : "=a"(value) : "Nd"((uint16_t)RODNIX_PCI_CFG_DATA));

    uint32_t shift = (reg & 0x3u) * 8u;
    if (width == 1u) {
        return (value >> shift) & 0xFFu;
    }
    if (width == 2u) {
        shift = (reg & 0x2u) * 8u;
        return (value >> shift) & 0xFFFFu;
    }
    return value;
}

static inline void pci_write_config(device_t dev, uint32_t reg, uint32_t value, uint32_t width)
{
    uint32_t addr = pci_cfg_addr(dev, reg);
    uint32_t cur = 0;
    __asm__ volatile ("outl %%eax, %0" : : "Nd"((uint16_t)RODNIX_PCI_CFG_ADDR), "a"(addr));
    __asm__ volatile ("inl %1, %%eax" : "=a"(cur) : "Nd"((uint16_t)RODNIX_PCI_CFG_DATA));

    if (width == 1u) {
        uint32_t shift = (reg & 0x3u) * 8u;
        cur &= ~(0xFFu << shift);
        cur |= (value & 0xFFu) << shift;
    } else if (width == 2u) {
        uint32_t shift = (reg & 0x2u) * 8u;
        cur &= ~(0xFFFFu << shift);
        cur |= (value & 0xFFFFu) << shift;
    } else {
        cur = value;
    }

    __asm__ volatile ("outl %%eax, %0" : : "Nd"((uint16_t)RODNIX_PCI_CFG_ADDR), "a"(addr));
    __asm__ volatile ("outl %%eax, %0" : : "Nd"((uint16_t)RODNIX_PCI_CFG_DATA), "a"(cur));
}

static inline int pci_find_cap(device_t dev, int capability, uint32_t* offset_out)
{
    if (!dev || !offset_out) {
        return -1;
    }
    uint16_t status = (uint16_t)pci_read_config(dev, PCIR_STATUS, 2);
    if ((status & PCI_STATUS_CAP_LIST) == 0) {
        return -1;
    }

    uint32_t off = pci_read_config(dev, PCIR_CAP_PTR, 1) & 0xFCu;
    for (uint32_t i = 0; i < 48u && off >= 0x40u; i++) {
        uint32_t id = pci_read_config(dev, off + 0u, 1);
        uint32_t next = pci_read_config(dev, off + 1u, 1) & 0xFCu;
        if ((int)id == capability) {
            *offset_out = off;
            return 0;
        }
        if (next == 0u || next == off) {
            break;
        }
        off = next;
    }
    return -1;
}

#define pci_get_devid(_d) pci_get_device((_d))

#endif /* _RODNIX_COMPAT_DEV_PCI_PCIVAR_H */
