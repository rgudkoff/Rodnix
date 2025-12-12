/**
 * @file hid_kbd.c
 * @brief HID Keyboard driver implementation
 * 
 * Minimal HID keyboard driver that publishes keyboard service.
 */

#include "hid_kbd.h"
#include "hid.h"
#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/fabric/service/service.h"
#include "../../../kernel/core/interrupts.h"
#include "../../../kernel/arch/x86_64/keyboard.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Keyboard service state */
static keyboard_ops_t keyboard_ops = {0};
static fabric_service_t keyboard_service = {0};

/* Read keyboard event */
static int keyboard_read_event(keyboard_event_t *event)
{
    if (!event) {
        return -1;
    }
    
    extern char keyboard_read_char(void);
    char c = keyboard_read_char();
    
    if (c == 0) {
        return 0; /* No event */
    }
    
    event->key_code = (uint8_t)c;
    event->pressed = true; /* Simplified - always pressed */
    
    return 1;
}

/* Check if keyboard has event */
static bool keyboard_has_event(void)
{
    extern bool keyboard_has_input(void);
    return keyboard_has_input();
}

/* Probe function - match HID keyboard devices */
static bool hid_kbd_probe(fabric_device_t *dev)
{
    if (!dev) {
        return false;
    }
    
    /* Match HID keyboard class (PCI or PS/2) */
    if (dev->class_code == PCI_CLASS_HID && 
        dev->subclass == PCI_SUBCLASS_HID_KBD) {
        return true;
    }
    
    /* Match PS/2 keyboard by name */
    if (dev->name && dev->name[0] == 'p' && dev->name[1] == 's' && 
        dev->name[2] == '2' && dev->name[3] == '-') {
        /* PS/2 device */
        return true;
    }
    
    return false;
}

/* Keyboard IRQ handler for Fabric */
static void keyboard_irq_handler(int vector, void *arg)
{
    (void)vector;
    (void)arg;
    
    /* Read scan code from keyboard port */
    uint8_t scan_code;
    __asm__ volatile ("inb %1, %0" : "=a"(scan_code) : "Nd"((uint16_t)0x60));
    
    /* Add to keyboard buffer using existing keyboard functions */
    /* Extended scan code handling is done in keyboard_buffer_put_raw */
    extern void keyboard_buffer_put_raw(uint8_t scan_code);
    keyboard_buffer_put_raw(scan_code);
}

/* Attach function - register IRQ and publish service */
static int hid_kbd_attach(fabric_device_t *dev)
{
    (void)dev;
    
    /* Initialize keyboard hardware (minimal setup) */
    extern void keyboard_hw_init(void);
    keyboard_hw_init();
    
    /* Register keyboard IRQ (IRQ 1 = vector 33) */
    if (fabric_request_irq(33, keyboard_irq_handler, NULL) != 0) {
        return -1;
    }
    
    /* Enable keyboard IRQ */
    extern bool ioapic_is_available(void);
    extern void apic_enable_irq(uint8_t irq);
    extern void pic_enable_irq(uint8_t irq);
    
    if (ioapic_is_available()) {
        apic_enable_irq(1);
    } else {
        pic_enable_irq(1);
    }
    
    /* Initialize keyboard ops */
    keyboard_ops.read_event = keyboard_read_event;
    keyboard_ops.has_event = keyboard_has_event;
    
    /* Initialize keyboard service */
    keyboard_service.name = "keyboard";
    keyboard_service.ops = &keyboard_ops;
    keyboard_service.context = NULL;
    
    /* Publish keyboard service */
    if (fabric_service_publish(&keyboard_service) != 0) {
        return -1;
    }
    
    return 0;
}

/* Detach function */
static void hid_kbd_detach(fabric_device_t *dev)
{
    (void)dev;
    /* TODO: Unregister IRQ and service */
}

/* HID Keyboard driver */
static fabric_driver_t hid_kbd_driver = {
    .name = "hid_kbd",
    .probe = hid_kbd_probe,
    .attach = hid_kbd_attach,
    .detach = hid_kbd_detach,
    .suspend = NULL,
    .resume = NULL
};

/* Note: Keyboard IRQ is handled by existing keyboard driver */
/* This driver just publishes the service for other subsystems */

/* Initialize HID keyboard driver */
void hid_kbd_init(void)
{
    fabric_driver_register(&hid_kbd_driver);
}

