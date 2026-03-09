#ifndef _RODNIX_COMPAT_DEV_PCI_PCIVAR_H
#define _RODNIX_COMPAT_DEV_PCI_PCIVAR_H

#include <stdint.h>
#include "../../sys/bus.h"
#include "../../../kernel/fabric/bus/pci.h"

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

#define pci_get_devid(_d) pci_get_device((_d))

#endif /* _RODNIX_COMPAT_DEV_PCI_PCIVAR_H */
