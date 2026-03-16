#ifndef _RODNIX_FABRIC_DISPATCHER_H
#define _RODNIX_FABRIC_DISPATCHER_H

#include "device/device.h"
#include "driver/driver.h"
#include <stdint.h>

uint32_t fabric_dispatcher_poll(fabric_device_t* const* devices,
                                uint32_t device_count,
                                fabric_driver_t* const* drivers,
                                uint32_t driver_count,
                                fabric_device_t** matched_devices,
                                fabric_driver_t** matched_drivers,
                                uint32_t max_matches);
uint32_t fabric_dispatcher_poll_all(fabric_device_t* const* devices,
                                    uint32_t device_count,
                                    fabric_driver_t* const* drivers,
                                    uint32_t driver_count);

#endif /* _RODNIX_FABRIC_DISPATCHER_H */
