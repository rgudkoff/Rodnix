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
#include "../../../kernel/input/input.h"
#include "../../../kernel/arch/x86_64/pic.h"
#include "../../../include/console.h"
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
    
    int c = input_read_char();
    
    if (c == -1) {
        return 0; /* No event */
    }
    
    event->key_code = (uint8_t)c;
    event->pressed = true; /* Simplified - always pressed */
    
    return 1;
}

/* Check if keyboard has event */
static bool keyboard_has_event(void)
{
    return input_has_char();
}

/* Probe function - match HID keyboard devices */
static bool hid_kbd_probe(fabric_device_t *dev)
{
    extern void kprintf(const char* fmt, ...);
    
    if (!dev) {
        kputs("[HID-KBD] probe: dev is NULL\n");
        return false;
    }
    
    kprintf("[HID-KBD] probe: checking device '%s' (class=0x%02X, subclass=0x%02X)\n",
            dev->name ? dev->name : "(null)", dev->class_code, dev->subclass);
    
    /* Match HID keyboard class (PCI or PS/2) */
    if (dev->class_code == PCI_CLASS_HID && 
        dev->subclass == PCI_SUBCLASS_HID_KBD) {
        kputs("[HID-KBD] probe: MATCH (HID class)\n");
        return true;
    }
    
    /* Match PS/2 keyboard by name */
    if (dev->name && dev->name[0] == 'p' && dev->name[1] == 's' && 
        dev->name[2] == '2' && dev->name[3] == '-') {
        /* PS/2 device */
        kputs("[HID-KBD] probe: MATCH (PS/2 by name)\n");
        return true;
    }
    
    kputs("[HID-KBD] probe: NO MATCH\n");
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
    
    /* DIAGNOSTIC: Simple VGA output for critical diagnostics */
    static volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
    static uint32_t kb_debug_pos = 0;
    
    /* Output scan code as hex to VGA (simple, no kprintf) */
    if (kb_debug_pos < 40) {
        uint32_t pos = 80 * 11 + kb_debug_pos * 2;
        uint8_t high = (scan_code >> 4) & 0x0F;
        uint8_t low = scan_code & 0x0F;
        vga_debug[pos] = 0x0F00 | (high < 10 ? ('0' + high) : ('A' + high - 10));
        vga_debug[pos + 1] = 0x0F00 | (low < 10 ? ('0' + low) : ('A' + low - 10));
        kb_debug_pos++;
    }
    
    /* Handle extended scan code prefix (0xE0) */
    /* Pass it directly to InputCore, which will handle it */
    if (scan_code == 0xE0) {
        kputs("[EXT]\n");
        input_push_scancode(0xE0, true);
        return;
    }
    
    /* Determine if key is pressed or released */
    bool pressed = (scan_code & 0x80) == 0;
    uint8_t key_code = scan_code & 0x7F;
    
    kprintf("key=0x%02X %s\n", key_code, pressed ? "PRESS" : "RELEASE");
    
    /* Push raw scan code to InputCore */
    /* InputCore handles translation to ASCII */
    input_push_scancode(key_code, pressed);
}

/* Attach function - register IRQ and publish service */
static int hid_kbd_attach(fabric_device_t *dev)
{
    (void)dev;
    
    extern void kprintf(const char* fmt, ...);
    kputs("[HID-KBD] Attaching keyboard driver\n");
    
    /* Initialize InputCore (system input layer) */
    input_init_keyboard();
    
    /* Initialize PS/2 keyboard: enable keyboard (send 0xF4) */
    kputs("[HID-KBD] Initializing PS/2 keyboard hardware\n");
    /* Wait for keyboard controller to be ready */
    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile ("pause");
    }
    
    /* Read status to clear any pending data */
    uint8_t status;
    __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x64));
    
    /* Send enable keyboard command (0xF4) */
    /* Wait for input buffer to be empty */
    int timeout = 1000;
    while (timeout-- > 0) {
        __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x64));
        if ((status & 0x02) == 0) { /* Input buffer empty */
            break;
        }
        __asm__ volatile ("pause");
    }
    
    /* Send enable command to keyboard */
    __asm__ volatile ("outb %%al, %1" : : "a"((uint8_t)0xF4), "Nd"((uint16_t)0x60));
    kputs("[HID-KBD] Keyboard enable command sent (0xF4)\n");
    
    /* Small delay to allow keyboard to process command */
    for (volatile int i = 0; i < 10000; i++) {
        __asm__ volatile ("pause");
    }
    
    /* Read and discard any pending scan codes from keyboard */
    kputs("[HID-KBD] Clearing keyboard buffer\n");
    for (int i = 0; i < 10; i++) {
        __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x64));
        if (status & 0x01) { /* Output buffer full */
            uint8_t dummy;
            __asm__ volatile ("inb %1, %0" : "=a"(dummy) : "Nd"((uint16_t)0x60));
        } else {
            break;
        }
    }
    
    /* Register keyboard IRQ (IRQ 1 = vector 33) */
    kprintf("[HID-KBD] Registering IRQ handler (vector 33)\n");
    if (fabric_request_irq(33, keyboard_irq_handler, NULL) != 0) {
        kputs("[HID-KBD] ERROR: Failed to register IRQ\n");
        return -1;
    }
    
    /* Enable keyboard IRQ - use I/O APIC if available, otherwise fallback to PIC */
    extern bool ioapic_is_available(void);
    extern void apic_enable_irq(uint8_t irq);
    extern void pic_enable_irq(uint8_t irq);
    
    if (ioapic_is_available()) {
        kprintf("[HID-KBD] Enabling keyboard IRQ (IRQ 1) via I/O APIC\n");
        apic_enable_irq(1);
    } else {
        /* I/O APIC not available - fallback to PIC */
        kputs("[HID-KBD] WARNING: I/O APIC not available, using PIC\n");
        pic_enable_irq(1);
    }
    
    kputs("[HID-KBD] Keyboard driver attached successfully\n");
    
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
    extern void kputs(const char* str);
    kputs("[HID-KBD] Initializing HID keyboard driver\n");
    fabric_driver_register(&hid_kbd_driver);
    kputs("[HID-KBD] Driver registered\n");
}

