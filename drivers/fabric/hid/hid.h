/**
 * @file hid.h
 * @brief HID (Human Interface Device) common definitions
 */

#ifndef _RODNIX_DRIVERS_FABRIC_HID_H
#define _RODNIX_DRIVERS_FABRIC_HID_H

#include <stdint.h>
#include <stdbool.h>

/* HID Class Code */
#define PCI_CLASS_HID         0x03
#define PCI_SUBCLASS_HID_KBD  0x01

/* Keyboard event structure */
typedef struct {
    uint8_t key_code;
    bool pressed;
} keyboard_event_t;

/* Keyboard service operations */
typedef struct {
    int (*read_event)(keyboard_event_t *event);
    bool (*has_event)(void);
} keyboard_ops_t;

#endif /* _RODNIX_DRIVERS_FABRIC_HID_H */

