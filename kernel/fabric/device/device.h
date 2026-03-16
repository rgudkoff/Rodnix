/**
 * @file device.h
 * @brief Fabric device interface
 */

#ifndef _RODNIX_FABRIC_DEVICE_H
#define _RODNIX_FABRIC_DEVICE_H

#include <stdint.h>

typedef struct fabric_device {
    const char *name;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    void *bus_private;   /* Bus-specific data (PCI BDF, etc.) */
    void *driver_state;  /* Driver private state */
} fabric_device_t;

#endif /* _RODNIX_FABRIC_DEVICE_H */

