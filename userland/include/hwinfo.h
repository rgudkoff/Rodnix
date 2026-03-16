#ifndef _RODNIX_USERLAND_HWINFO_H
#define _RODNIX_USERLAND_HWINFO_H

#include <stdint.h>

#define HWINFO_BAR_COUNT 6u
#define HWINFO_PROP_MAX  12u

enum {
    HWINFO_PROP_U32 = 1,
    HWINFO_PROP_STR = 2
};

typedef struct hwprop_info {
    char key[24];
    uint32_t type;
    uint32_t u32;
    char str[32];
} hwprop_info_t;

typedef struct hwdev_info {
    char name[32];
    char driver[32];
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t attached;
    uint8_t is_pci;
    uint8_t dispatch_state;
    uint8_t reserved0;
    uint16_t attach_attempts;
    uint32_t property_count;
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint8_t pci_revision;
    uint8_t pci_header_type;
    uint8_t pci_primary_bus;
    uint8_t pci_secondary_bus;
    uint8_t pci_subordinate_bus;
    uint8_t pci_interrupt_line;
    uint8_t pci_interrupt_pin;
    uint8_t pci_cap_ptr;
    uint8_t pci_cap_count;
    uint16_t reserved1;
    uint16_t pci_command;
    uint16_t pci_status;
    uint32_t pci_capability_bits;
    uint32_t bars[HWINFO_BAR_COUNT];
    hwprop_info_t properties[HWINFO_PROP_MAX];
} hwdev_info_t;

#endif /* _RODNIX_USERLAND_HWINFO_H */
