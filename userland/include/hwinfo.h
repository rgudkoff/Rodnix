#ifndef _RODNIX_USERLAND_HWINFO_H
#define _RODNIX_USERLAND_HWINFO_H

#include <stdint.h>

#define HWINFO_BAR_COUNT 6u

typedef struct hwdev_info {
    char name[32];
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t attached;
    uint8_t is_pci;
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint8_t pci_revision;
    uint8_t pci_header_type;
    uint16_t pci_command;
    uint16_t pci_status;
    uint32_t bars[HWINFO_BAR_COUNT];
} hwdev_info_t;

#endif /* _RODNIX_USERLAND_HWINFO_H */
