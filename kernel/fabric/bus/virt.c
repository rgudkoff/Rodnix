/**
 * @file virt.c
 * @brief Virtual bus implementation for testing
 */

#include "virt.h"
#include "bus.h"
#include "../fabric.h"
#include "../device/device.h"
#include <stddef.h>

/* Dummy device for testing */
static fabric_device_t dummy_device = {
    .name = "virt-dummy",
    .vendor_id = 0x1234,
    .device_id = 0x5678,
    .class_code = 0xFF,
    .subclass = 0x00,
    .prog_if = 0x00,
    .bus_private = NULL,
    .driver_state = NULL
};

static void virt_enumerate(void)
{
    fabric_device_publish(&dummy_device);
}

static fabric_bus_t virt_bus = {
    .name = "virt",
    .enumerate = virt_enumerate,
    .rescan = NULL
};

void virt_bus_init(void)
{
    fabric_bus_register(&virt_bus);
}

