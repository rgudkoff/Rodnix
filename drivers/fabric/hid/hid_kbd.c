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
#include "../../../kernel/arch/x86_64/apic.h"
#include "../../../include/console.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>


static inline uint8_t kbd_read_status(void)
{
    uint8_t status;
    __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x64));
    return status;
}

static inline uint8_t kbd_read_data(void)
{
    uint8_t data;
    __asm__ volatile ("inb %1, %0" : "=a"(data) : "Nd"((uint16_t)0x60));
    return data;
}

static inline void kbd_write_cmd(uint8_t cmd)
{
    __asm__ volatile ("outb %%al, %1" : : "a"(cmd), "Nd"((uint16_t)0x64));
}

static inline void kbd_write_data(uint8_t data)
{
    __asm__ volatile ("outb %%al, %1" : : "a"(data), "Nd"((uint16_t)0x60));
}

static void kbd_wait_input_empty(void)
{
    for (int i = 0; i < 10000; i++) {
        if ((kbd_read_status() & 0x02) == 0) {
            return;
        }
        __asm__ volatile ("pause");
    }
}

static void kbd_wait_output_full(void)
{
    for (int i = 0; i < 10000; i++) {
        if (kbd_read_status() & 0x01) {
            return;
        }
        __asm__ volatile ("pause");
    }
}

/* Keyboard service state */
static keyboard_ops_t keyboard_ops = {0};
static fabric_service_t keyboard_service = {0};

/* Lock-free queue for scan codes from interrupt handler */
#define KBD_SCANCODE_QUEUE_SIZE 64
typedef struct {
    uint8_t scan_code;
    bool pressed;
} scancode_entry_t;

static volatile scancode_entry_t scancode_queue[KBD_SCANCODE_QUEUE_SIZE];
static volatile uint32_t scancode_queue_head = 0;
static volatile uint32_t scancode_queue_tail = 0;

void hid_kbd_flush_queue(void)
{
    interrupts_disable();

    /* Drain any pending bytes from controller output buffer */
    while (kbd_read_status() & 0x01) {
        (void)kbd_read_data();
    }

    scancode_queue_head = 0;
    scancode_queue_tail = 0;
    __asm__ volatile ("" ::: "memory");

    interrupts_enable();
}


/* Read keyboard event */
static int keyboard_read_event(keyboard_event_t *event)
{
    if (!event) {
        return -1;
    }
    
    /* Process queued scan codes first (input_read_char does this) */
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
    /* Process queued scan codes first (input_has_char does this) */
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
    
    kprintf("[HID-KBD] probe: checking device '%s' (class=%x, subclass=%x)\n",
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

/* Process queued scan codes (internal implementation) */
static void keyboard_process_queue_internal(void)
{
    while (scancode_queue_head != scancode_queue_tail) {
        uint32_t head = scancode_queue_head;
        uint32_t next_head = (head + 1) & (KBD_SCANCODE_QUEUE_SIZE - 1);

        scancode_entry_t entry = scancode_queue[head];
        scancode_queue_head = next_head;
        
        /* Now process in safe context */
        input_push_scancode(entry.scan_code, entry.pressed);
    }
}

/* Process queued scan codes (called from normal context, not IRQ) */
void input_process_queue(void)
{
    keyboard_process_queue_internal();
}

/* Keyboard IRQ handler for Fabric - minimal processing */
static void keyboard_irq_handler(int vector, void *arg)
{
    (void)vector;
    (void)arg;
 
    
    /* In interrupt context, do MINIMAL work:
     * 1. Read scan code from hardware
     * 2. Put it in lock-free queue
     * 3. Return immediately
     * Processing happens later in safe context */
    
    /* Read scan code from keyboard port */
    uint8_t status = kbd_read_status();
    if ((status & 0x01) == 0) {
        return;
    }

    uint8_t scan_code;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(scan_code) : "d"((uint16_t)0x60));

    (void)scan_code;

    
    /* Ignore controller responses (ACK/RESEND/SELF-TEST) */
    if (scan_code == 0xFA || scan_code == 0xFE || scan_code == 0xAA) {
        return;
    }

    /* Determine if key is pressed or released */
    bool pressed = (scan_code & 0x80) == 0;
    uint8_t key_code = scan_code & 0x7F;
    
    /* Handle extended scan code prefix (0xE0) */
    if (scan_code == 0xE0) {
        key_code = 0xE0;
        pressed = true;
    }
    
    /* Put scan code in lock-free queue */
    uint32_t tail = scancode_queue_tail;
    uint32_t next_tail = (tail + 1) & (KBD_SCANCODE_QUEUE_SIZE - 1);
    
    /* Check if queue is full (simple check, may lose data if full) */
    if (next_tail != scancode_queue_head) {
        scancode_queue[tail].scan_code = key_code;
        scancode_queue[tail].pressed = pressed;
        /* Memory barrier to ensure data is written before updating tail */
        __asm__ volatile ("" ::: "memory");
        scancode_queue_tail = next_tail;
    }
    /* If queue is full, scan code is lost (better than deadlock) */
    
}

/* Attach function - register IRQ and publish service */
static int hid_kbd_attach(fabric_device_t *dev)
{
    (void)dev;
    
    kputs("[HID-KBD] Attaching keyboard driver\n");
    
    /* Initialize lock-free queue */
    scancode_queue_head = 0;
    scancode_queue_tail = 0;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    
    /* Initialize InputCore (system input layer) */
    input_init_keyboard();
    
    /* Initialize PS/2 controller + keyboard */
    kputs("[HID-KBD] Initializing PS/2 keyboard hardware\n");

    /* Enable keyboard interface on controller */
    kbd_wait_input_empty();
    kbd_write_cmd(0xAE); /* Enable keyboard interface */

    /* Read controller config byte */
    kbd_wait_input_empty();
    kbd_write_cmd(0x20); /* Read config */
    kbd_wait_output_full();
    uint8_t config = kbd_read_data();
    {
        extern void kprintf(const char* fmt, ...);
        kprintf("[HID-KBD] Controller config read: 0x%x\n", (unsigned)config);
    }

    /* Ensure IRQ1 enabled, system flag set, keyboard interface enabled */
    config |= 0x01;      /* IRQ1 enable */
    config |= 0x04;      /* System flag */
    config &= ~(0x10);   /* Keyboard disable = 0 */
    config |= 0x40;      /* Enable translation (set2 -> set1) */

    /* Write controller config byte */
    kbd_wait_input_empty();
    kbd_write_cmd(0x60);
    kbd_wait_input_empty();
    kbd_write_data(config);
    {
        extern void kprintf(const char* fmt, ...);
        kprintf("[HID-KBD] Controller config written: 0x%x\n", (unsigned)config);
    }

    /* Flush any pending data */
    kputs("[HID-KBD] Clearing keyboard buffer\n");
    for (int i = 0; i < 16; i++) {
        if (kbd_read_status() & 0x01) {
            (void)kbd_read_data();
        } else {
            break;
        }
    }

    /* Enable keyboard scanning (0xF4) */
    kbd_wait_input_empty();
    kbd_write_data(0xF4);
    kputs("[HID-KBD] Keyboard enable command sent (0xF4)\n");

    /* Drain ACK if present */
    kbd_wait_output_full();
    uint8_t ack = kbd_read_data();
    {
        extern void kprintf(const char* fmt, ...);
        kprintf("[HID-KBD] Keyboard ACK: 0x%x\n", (unsigned)ack);
    }
    
    /* Register IRQ1 through Fabric and enable it if possible */
    int irq_vector = 32 + 1; /* IRQ1 -> vector 33 */
    if (fabric_request_irq(irq_vector, keyboard_irq_handler, NULL) != 0) {
        kputs("[HID-KBD] WARNING: IRQ1 registration failed; keeping polling fallback\n");
        input_set_polling_enabled(true);
    } else {
        if (apic_is_available()) {
            if (ioapic_is_available()) {
                kputs("[HID-KBD] IOAPIC present, forcing PIC routing for IRQ1\n");
                pic_enable_irq(1);
            } else {
                kputs("[HID-KBD] IRQ1 routed via PIC (LAPIC EOI)\n");
                pic_enable_irq(1);
            }
        } else {
            kputs("[HID-KBD] IRQ1 routed via PIC\n");
            pic_enable_irq(1);
        }
        /* IRQ path is active; disable polling to avoid duplicate reads */
        input_set_polling_enabled(false);
        kputs("[HID-KBD] IRQ1 enabled via Fabric (polling disabled)\n");
    }
    kputs("[HID-KBD] Keyboard driver attached successfully\n");
    
    /* Temporarily disable interrupts during initialization to avoid interference */
    __asm__ volatile ("cli"); /* Disable interrupts */
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    
    /* Initialize keyboard ops */
    kputs("[HID-KBD] Initializing keyboard ops...\n");
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    kputs("[HID-KBD] Setting read_event pointer...\n");
    keyboard_ops.read_event = keyboard_read_event;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    kputs("[HID-KBD] Setting has_event pointer...\n");
    keyboard_ops.has_event = keyboard_has_event;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    kputs("[HID-KBD] Keyboard ops initialized\n");
    
    /* Initialize keyboard service */
    kputs("[HID-KBD] Initializing keyboard service...\n");
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    kputs("[HID-KBD] Setting service name...\n");
    keyboard_service.name = "keyboard";
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    kputs("[HID-KBD] Setting service ops pointer...\n");
    keyboard_service.ops = &keyboard_ops;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    kputs("[HID-KBD] Setting service context...\n");
    keyboard_service.context = NULL;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    kputs("[HID-KBD] Keyboard service initialized\n");
    
    /* Re-enable interrupts */
    kputs("[HID-KBD] Re-enabling interrupts (sti)...\n");
    __asm__ volatile ("" ::: "memory"); /* Memory barrier before sti */
    
    
    __asm__ volatile ("sti"); /* Enable interrupts */
    __asm__ volatile ("" ::: "memory"); /* Memory barrier after sti */
    
    
    kputs("[HID-KBD] Interrupts re-enabled\n");
    
    /* Small delay */
    for (volatile int i = 0; i < 10000; i++) {
        __asm__ volatile ("pause");
    }
    
    /* Small delay to allow any pending interrupts to be processed */
    for (volatile int i = 0; i < 5000; i++) {
        __asm__ volatile ("pause");
    }
    
    /* Publish keyboard service */
    kputs("[HID-KBD] Publishing keyboard service...\n");
    if (fabric_service_publish(&keyboard_service) != 0) {
        kputs("[HID-KBD] ERROR: Failed to publish keyboard service\n");
        return -1;
    }
    kputs("[HID-KBD] Keyboard service published successfully\n");
    
    kputs("[HID-KBD] hid_kbd_attach() returning 0\n");
    return 0;
}

/* Detach function */
static void hid_kbd_detach(fabric_device_t *dev)
{
    (void)dev;
    int irq_vector = 32 + 1; /* IRQ1 -> vector 33 */
    fabric_free_irq(irq_vector, keyboard_irq_handler);

    if (apic_is_available()) {
        if (ioapic_is_available()) {
            apic_disable_irq(1);
        } else {
            pic_disable_irq(1);
        }
    } else {
        pic_disable_irq(1);
    }

    input_set_polling_enabled(true);
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
    kputs("[HID-KBD] Calling fabric_driver_register...\n");
    int result = fabric_driver_register(&hid_kbd_driver);
    kputs("[HID-KBD] fabric_driver_register returned\n");
    if (result != 0) {
        kputs("[HID-KBD] ERROR: fabric_driver_register failed\n");
    }
    kputs("[HID-KBD] Driver registered\n");
}
