/**
 * @file usb.c
 * @brief USB fabric bus registration
 */

#include "../fabric.h"
#include "bus.h"
#include "../../../include/console.h"
#include "../../../include/error.h"

static void usb_bus_enumerate(void)
{
}

static void usb_bus_rescan(void)
{
}

static fabric_bus_t usb_bus = {
    .name = "usb",
    .enumerate = usb_bus_enumerate,
    .rescan = usb_bus_rescan
};

void usb_bus_init(void)
{
    int rc = fabric_bus_register(&usb_bus);
    if (rc == RDNX_OK) {
        kputs("[USB] bus registered\n");
    } else {
        kputs("[USB] bus register failed\n");
    }
}
