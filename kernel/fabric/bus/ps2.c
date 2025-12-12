/**
 * @file ps2.c
 * @brief PS/2 bus implementation
 * 
 * Legacy PS/2 bus for keyboard and mouse devices.
 */

#include "ps2.h"
#include "bus.h"
#include "../fabric.h"
#include "../device/device.h"
#include "../../../include/console.h"
#include <stddef.h>

/* PS/2 Keyboard device */
static fabric_device_t ps2_keyboard_device = {
    .name = "ps2-keyboard",
    .vendor_id = 0x0001,  /* Legacy PS/2 */
    .device_id = 0x0001,  /* Keyboard */
    .class_code = 0x03,   /* HID class (PCI_CLASS_HID) */
    .subclass = 0x01,     /* Keyboard subclass (PCI_SUBCLASS_HID_KBD) */
    .prog_if = 0x00,
    .bus_private = NULL,
    .driver_state = NULL
};

static void ps2_enumerate(void)
{
    extern void kputs(const char* str);
    kputs("[PS2-BUS] Enumerating devices...\n");
    
    /* Publish PS/2 keyboard device */
    fabric_device_publish(&ps2_keyboard_device);
    
    kputs("[PS2-BUS] PS/2 keyboard device published\n");
}

static fabric_bus_t ps2_bus = {
    .name = "ps2",
    .enumerate = ps2_enumerate,
    .rescan = NULL
};

void ps2_bus_init(void)
{
    extern void kputs(const char* str);
    kputs("[PS2-BUS] Initializing PS/2 bus\n");
    fabric_bus_register(&ps2_bus);
    kputs("[PS2-BUS] PS/2 bus registered\n");
}

