/**
 * @file pci.h
 * @brief PCI bus interface
 */

#ifndef _RODNIX_FABRIC_BUS_PCI_H
#define _RODNIX_FABRIC_BUS_PCI_H

#include <stdint.h>

#define PCI_BAR_COUNT 6u

typedef struct pci_device_info {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t revision_id;
    uint8_t header_type;
    uint16_t command;
    uint16_t status;
    uint32_t bars[PCI_BAR_COUNT];
} pci_device_info_t;

void pci_bus_init(void);

#endif /* _RODNIX_FABRIC_BUS_PCI_H */
