/**
 * @file driver.h
 * @brief Fabric driver interface
 */

#ifndef _RODNIX_FABRIC_DRIVER_H
#define _RODNIX_FABRIC_DRIVER_H

#include <stdbool.h>
#include "../device/device.h"

typedef struct fabric_driver {
    const char *name;
    bool (*probe)(fabric_device_t *dev);
    int  (*attach)(fabric_device_t *dev);
    void (*detach)(fabric_device_t *dev);
    void (*suspend)(fabric_device_t *dev);
    void (*resume)(fabric_device_t *dev);
} fabric_driver_t;

#endif /* _RODNIX_FABRIC_DRIVER_H */

