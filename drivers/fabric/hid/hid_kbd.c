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

/* Lock-free queue for scan codes from interrupt handler */
#define KBD_SCANCODE_QUEUE_SIZE 64
typedef struct {
    uint8_t scan_code;
    bool pressed;
} scancode_entry_t;

static volatile scancode_entry_t scancode_queue[KBD_SCANCODE_QUEUE_SIZE];
static volatile uint32_t scancode_queue_head = 0;
static volatile uint32_t scancode_queue_tail = 0;

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

/* Process queued scan codes (internal implementation) */
static void keyboard_process_queue_internal(void)
{
    while (scancode_queue_head != scancode_queue_tail) {
        uint32_t head = scancode_queue_head;
        uint32_t next_head = (head + 1) & (KBD_SCANCODE_QUEUE_SIZE - 1);
        
        if (next_head == scancode_queue_tail) {
            break; /* Queue empty */
        }
        
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
    
    /* DIAGNOSTIC: Mark that handler was called (VGA only, bottom of screen, RED) */
    static volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
    static uint32_t handler_call_count = 0;
    
    if (handler_call_count < 20) {
        vga_debug[80 * 19 + handler_call_count] = 0x0C00 | ('K');  /* RED */
        handler_call_count++;
    }
    
    /* In interrupt context, do MINIMAL work:
     * 1. Read scan code from hardware
     * 2. Put it in lock-free queue
     * 3. Return immediately
     * Processing happens later in safe context */
    
    /* Read scan code from keyboard port */
    uint8_t scan_code;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(scan_code) : "d"((uint16_t)0x60));
    
    /* DIAGNOSTIC: Show scan code on VGA (RED) */
    if (handler_call_count < 20) {
        uint32_t pos = 80 * 19 + handler_call_count;
        uint8_t high = (scan_code >> 4) & 0x0F;
        uint8_t low = scan_code & 0x0F;
        vga_debug[pos] = 0x0C00 | (high < 10 ? ('0' + high) : ('A' + high - 10));  /* RED */
        vga_debug[pos + 1] = 0x0C00 | (low < 10 ? ('0' + low) : ('A' + low - 10));  /* RED */
        handler_call_count += 2;
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
    
    /* DIAGNOSTIC: Mark handler exit (RED) */
    if (handler_call_count < 20) {
        vga_debug[80 * 19 + handler_call_count - 1] = 0x0C00 | ('X');  /* RED */
    }
}

/* Attach function - register IRQ and publish service */
static int hid_kbd_attach(fabric_device_t *dev)
{
    (void)dev;
    
    extern void kprintf(const char* fmt, ...);
    kputs("[HID-KBD] Attaching keyboard driver\n");
    
    /* Initialize lock-free queue */
    scancode_queue_head = 0;
    scancode_queue_tail = 0;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    
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
    
    /* ВРЕМЕННО: не регистрируем IRQ и не включаем IRQ1.
     * Ввод будет работать через поллинговый fallback в InputCore,
     * который сам читает порты 0x64/0x60 и вызывает input_push_scancode().
     */
    kputs("[HID-KBD] NOTE: Skipping IRQ registration; using polling fallback in InputCore\n");
    kputs("[HID-KBD] Keyboard driver attached successfully (polling mode)\n");
    
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
    
    /* Re-enable interrupts (but keyboard IRQ is still disabled) */
    kputs("[HID-KBD] Re-enabling interrupts (sti)...\n");
    __asm__ volatile ("" ::: "memory"); /* Memory barrier before sti */
    
    /* DIAGNOSTIC: Mark before sti on VGA (bottom of screen, RED) */
    static volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
    vga_debug[80 * 23] = 0x0C00 | ('S');  /* RED */
    vga_debug[80 * 23 + 1] = 0x0C00 | ('T');  /* RED */
    vga_debug[80 * 23 + 2] = 0x0C00 | ('I');  /* RED */
    
    __asm__ volatile ("sti"); /* Enable interrupts */
    __asm__ volatile ("" ::: "memory"); /* Memory barrier after sti */
    
    /* DIAGNOSTIC: Mark after sti on VGA - CRITICAL: This must appear! (RED) */
    vga_debug[80 * 18] = 0x0C00 | ('A');  /* RED - After sti */
    vga_debug[80 * 18 + 1] = 0x0C00 | ('F');  /* RED - After sti */
    vga_debug[80 * 23 + 3] = 0x0C00 | ('D');  /* RED */
    vga_debug[80 * 23 + 4] = 0x0C00 | ('1');  /* RED */
    
    kputs("[HID-KBD] Interrupts re-enabled (keyboard IRQ still disabled)\n");
    vga_debug[80 * 18 + 2] = 0x0C00 | ('2');  /* RED */
    
    /* Small delay */
    for (volatile int i = 0; i < 10000; i++) {
        __asm__ volatile ("pause");
    }
    vga_debug[80 * 18 + 3] = 0x0C00 | ('3');  /* RED */
    
    /* ВРЕМЕННО: полностью отключаем включение IRQ1 клавиатуры.
     * Всё ниже будет включено, когда IRQ-путь станет стабильным.
     */
    kputs("[HID-KBD] NOTE: Keyboard IRQ1 is DISABLED (polling mode only)\n");
    
    /* Small delay to allow any pending interrupts to be processed */
    for (volatile int i = 0; i < 5000; i++) {
        __asm__ volatile ("pause");
    }
    vga_debug[80 * 18 + 6] = 0x0C00 | ('D');  /* RED - Delay done */
    
    /* Publish keyboard service */
    kputs("[HID-KBD] Publishing keyboard service...\n");
    vga_debug[80 * 18 + 6] = 0x0C00 | ('P');  /* RED - Publishing */
    if (fabric_service_publish(&keyboard_service) != 0) {
        kputs("[HID-KBD] ERROR: Failed to publish keyboard service\n");
        vga_debug[80 * 18 + 7] = 0x0C00 | ('E');  /* RED - Error */
        return -1;
    }
    kputs("[HID-KBD] Keyboard service published successfully\n");
    vga_debug[80 * 18 + 7] = 0x0C00 | ('O');  /* RED - OK */
    
    kputs("[HID-KBD] hid_kbd_attach() returning 0\n");
    vga_debug[80 * 18 + 8] = 0x0C00 | ('R');  /* RED - Return */
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
    kputs("[HID-KBD] Calling fabric_driver_register...\n");
    int result = fabric_driver_register(&hid_kbd_driver);
    kputs("[HID-KBD] fabric_driver_register returned\n");
    if (result != 0) {
        kputs("[HID-KBD] ERROR: fabric_driver_register failed\n");
    }
    kputs("[HID-KBD] Driver registered\n");
}

