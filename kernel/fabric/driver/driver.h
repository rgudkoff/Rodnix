/**
 * @file driver.h
 * @brief Fabric driver interface
 */

#ifndef _RODNIX_FABRIC_DRIVER_H
#define _RODNIX_FABRIC_DRIVER_H

#include <stdbool.h>
#include "../device/device.h"

enum {
    FABRIC_MATCH_NONE = -1,
    FABRIC_MATCH_WEAK = 100,
    FABRIC_MATCH_GENERIC = 500,
    FABRIC_MATCH_BUS_EXACT = 750,
    FABRIC_MATCH_DEVICE_EXACT = 1000
};

typedef struct fabric_driver {
    const char *name;
    const fabric_property_t* match_properties;
    uint32_t match_property_count;
    int match_priority;
    int  (*match_score)(fabric_device_t *dev);
    bool (*probe)(fabric_device_t *dev);
    int  (*attach)(fabric_device_t *dev);
    int  (*publish)(fabric_device_t *dev);
    void (*detach)(fabric_device_t *dev);
    void (*suspend)(fabric_device_t *dev);
    void (*resume)(fabric_device_t *dev);
} fabric_driver_t;

#endif /* _RODNIX_FABRIC_DRIVER_H */
