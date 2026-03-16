/**
 * @file device.h
 * @brief Fabric device interface
 */

#ifndef _RODNIX_FABRIC_DEVICE_H
#define _RODNIX_FABRIC_DEVICE_H

#include <stdint.h>

#define FABRIC_PROPERTY_MAX 12u

typedef enum fabric_property_type {
    FABRIC_PROP_U32 = 1,
    FABRIC_PROP_STR = 2
} fabric_property_type_t;

typedef struct fabric_property {
    const char* key;
    uint32_t type;
    union {
        uint32_t u32;
        const char* str;
    } value;
} fabric_property_t;

typedef enum fabric_device_dispatch_state {
    FABRIC_DEV_DISPATCH_NONE = 0,
    FABRIC_DEV_DISPATCH_PENDING = 1,
    FABRIC_DEV_DISPATCH_MATCHED = 2,
    FABRIC_DEV_DISPATCH_ATTACHED = 3,
    FABRIC_DEV_DISPATCH_FAILED = 4
} fabric_device_dispatch_state_t;

typedef struct fabric_device {
    const char *name;
    const char *attached_driver_name;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  dispatch_state;
    uint8_t  reserved0;
    uint16_t attach_attempts;
    uint32_t property_count;
    fabric_property_t properties[FABRIC_PROPERTY_MAX];
    void *bus_private;   /* Bus-specific data (PCI BDF, etc.) */
    void *driver_state;  /* Driver private state */
} fabric_device_t;

#endif /* _RODNIX_FABRIC_DEVICE_H */
