/**
 * @file fabric.c
 * @brief Fabric core implementation
 * 
 * Fabric manages hardware as a graph of managed objects.
 * It handles device lifecycle and driver matching.
 */

#include "fabric.h"
#include "spin.h"
#include "bus/bus.h"
#include "device/device.h"
#include "driver/driver.h"
#include "service/service.h"
#include "../../include/console.h"
#include "../../core/interrupts.h"
#include <stddef.h>
#include <stdarg.h>

/* Maximum number of buses, drivers, devices, services */
#define MAX_BUSES    16
#define MAX_DRIVERS  64
#define MAX_DEVICES  256
#define MAX_SERVICES 64

/* Registries */
static fabric_bus_t* bus_registry[MAX_BUSES];
static fabric_driver_t* driver_registry[MAX_DRIVERS];
static fabric_device_t* device_registry[MAX_DEVICES];
static fabric_service_t* service_registry[MAX_SERVICES];

static uint32_t bus_count = 0;
static uint32_t driver_count = 0;
static uint32_t device_count = 0;
static uint32_t service_count = 0;

/* Spinlock for thread safety */
static spinlock_t fabric_lock;

/* IRQ handler registry */
#define MAX_IRQ_HANDLERS 64
typedef struct {
    int vector;
    fabric_irq_handler_t handler;
    void *arg;
    bool active;
} irq_handler_entry_t;

static irq_handler_entry_t irq_handlers[MAX_IRQ_HANDLERS];
static spinlock_t irq_lock;

/* Internal IRQ handler wrapper */
static void fabric_irq_wrapper(interrupt_context_t* ctx)
{
    int vector = ctx->vector;
    
    /* DIAGNOSTIC: Simple output to avoid kprintf in IRQ context */
    /* Use direct VGA output for critical diagnostics (bottom of screen, RED) */
    static volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
    static uint32_t debug_pos = 0;
    
    /* DIAGNOSTIC: Mark that wrapper was called (RED) */
    if (debug_pos < 40) {
        vga_debug[80 * 20 + debug_pos] = 0x0C00 | ('F');  /* RED */
        vga_debug[80 * 20 + debug_pos + 1] = 0x0C00 | ('0' + (vector % 10));  /* RED */
        debug_pos += 2;
    }
    
    /* Call all registered handlers for this vector */
    /* XNU-style: Avoid any operations that might use FPU */
    /* Use simple integer operations only */
    uint32_t i = 0;
    uint32_t handler_count = 0;
    while (i < MAX_IRQ_HANDLERS) {
        if (irq_handlers[i].active && irq_handlers[i].vector == vector) {
            /* DIAGNOSTIC: Mark before calling handler (RED) */
            if (debug_pos < 40) {
                vga_debug[80 * 20 + debug_pos] = 0x0C00 | ('H');  /* RED */
                debug_pos++;
            }
            irq_handlers[i].handler(vector, irq_handlers[i].arg);
            /* DIAGNOSTIC: Mark after calling handler (RED) */
            if (debug_pos < 40) {
                vga_debug[80 * 20 + debug_pos] = 0x0C00 | ('D');  /* RED */
                debug_pos++;
            }
            handler_count++;
        }
        i++; /* Increment manually to avoid potential FPU usage */
    }
    
    /* DIAGNOSTIC: Mark wrapper exit (RED) */
    if (debug_pos < 40) {
        vga_debug[80 * 20 + debug_pos] = 0x0C00 | ('E');  /* RED */
        debug_pos++;
    }
}

/* Initialize Fabric */
void fabric_init(void)
{
    /* Initialize spinlocks */
    spinlock_init(&fabric_lock);
    spinlock_init(&irq_lock);
    
    /* Clear registries */
    for (uint32_t i = 0; i < MAX_BUSES; i++) {
        bus_registry[i] = NULL;
    }
    for (uint32_t i = 0; i < MAX_DRIVERS; i++) {
        driver_registry[i] = NULL;
    }
    for (uint32_t i = 0; i < MAX_DEVICES; i++) {
        device_registry[i] = NULL;
    }
    for (uint32_t i = 0; i < MAX_SERVICES; i++) {
        service_registry[i] = NULL;
    }
    for (uint32_t i = 0; i < MAX_IRQ_HANDLERS; i++) {
        irq_handlers[i].active = false;
    }
    
    bus_count = 0;
    driver_count = 0;
    device_count = 0;
    service_count = 0;
    
    fabric_log("[fabric] Fabric initialized\n");
}

/* Register a bus */
int fabric_bus_register(fabric_bus_t *bus)
{
    if (!bus || !bus->name) {
        return -1;
    }
    
    spinlock_lock(&fabric_lock);
    
    if (bus_count >= MAX_BUSES) {
        spinlock_unlock(&fabric_lock);
        return -1;
    }
    
    bus_registry[bus_count++] = bus;
    
    spinlock_unlock(&fabric_lock);
    
    fabric_log("[fabric] bus registered: %s\n", bus->name);
    
    /* Enumerate devices immediately */
    if (bus->enumerate) {
        bus->enumerate();
    }
    
    return 0;
}

/* Register a driver */
int fabric_driver_register(fabric_driver_t *driver)
{
    if (!driver || !driver->name) {
        return -1;
    }
    
    spinlock_lock(&fabric_lock);
    
    if (driver_count >= MAX_DRIVERS) {
        spinlock_unlock(&fabric_lock);
        return -1;
    }
    
    driver_registry[driver_count++] = driver;
    
    spinlock_unlock(&fabric_lock);
    
    fabric_log("[fabric] driver registered: %s\n", driver->name);
    
    /* Try to match with existing devices */
    fabric_log("[fabric] Matching driver with devices (device_count=%u)\n", device_count);
    spinlock_lock(&fabric_lock);
    for (uint32_t i = 0; i < device_count; i++) {
        fabric_device_t *dev = device_registry[i];
        if (!dev || dev->driver_state) {
            continue; /* Already has driver */
        }
        
        /* Release lock before probe/attach */
        spinlock_unlock(&fabric_lock);
        
        fabric_log("[fabric] Trying to probe device %u: %s\n", i, dev->name ? dev->name : "(null)");
        if (driver->probe && driver->probe(dev)) {
            fabric_log("[fabric] Probe matched, calling attach\n");
            if (driver->attach && driver->attach(dev) == 0) {
                fabric_log("[fabric] Attach successful, will mark device as attached\n");
                /* Mark device as attached - need to reacquire lock first */
                extern void kputs(const char* str);
                kputs("[FABRIC] Reacquiring lock to mark device as attached...\n");
                spinlock_lock(&fabric_lock);
                kputs("[FABRIC] Lock reacquired, marking device as attached...\n");
                dev->driver_state = driver; /* Mark as attached */
                spinlock_unlock(&fabric_lock);
                kputs("[FABRIC] Device marked as attached, lock released\n");
                fabric_log("[fabric] driver attached: %s -> %s\n", 
                         driver->name, dev->name);
            } else {
                fabric_log("[fabric] Attach failed\n");
            }
        } else {
            fabric_log("[fabric] Probe did not match\n");
        }
        
        /* Reacquire lock for next iteration */
        kputs("[FABRIC] Reacquiring lock for next iteration...\n");
        spinlock_lock(&fabric_lock);
        kputs("[FABRIC] Lock reacquired for next iteration\n");
    }
    spinlock_unlock(&fabric_lock);
    
    fabric_log("[fabric] Driver registration complete\n");
    return 0;
}

/* Publish a device */
int fabric_device_publish(fabric_device_t *device)
{
    if (!device || !device->name) {
        return -1;
    }
    
    spinlock_lock(&fabric_lock);
    
    if (device_count >= MAX_DEVICES) {
        spinlock_unlock(&fabric_lock);
        return -1;
    }
    
    device_registry[device_count++] = device;
    device->driver_state = NULL; /* No driver yet */
    
    spinlock_unlock(&fabric_lock);
    
    fabric_log("[fabric] device found: vendor=0x%04x device=0x%04x class=0x%02x\n",
               device->vendor_id, device->device_id, device->class_code);
    
    /* Try to match with drivers */
    spinlock_lock(&fabric_lock);
    for (uint32_t i = 0; i < driver_count; i++) {
        fabric_driver_t *driver = driver_registry[i];
        if (!driver || !driver->probe) {
            continue;
        }
        
        /* Release lock before probe/attach */
        spinlock_unlock(&fabric_lock);
        
        if (driver->probe(device)) {
            if (driver->attach && driver->attach(device) == 0) {
                device->driver_state = driver; /* Mark as attached */
                fabric_log("[fabric] driver attached: %s -> %s\n", 
                         driver->name, device->name);
                return 0;
            }
        }
        
        spinlock_lock(&fabric_lock);
    }
    spinlock_unlock(&fabric_lock);
    
    return 0;
}

/* Publish a service */
int fabric_service_publish(fabric_service_t *service)
{
    extern void kputs(const char* str);
    
    kputs("[FABRIC-SVC] fabric_service_publish() called\n");
    
    if (!service || !service->name) {
        kputs("[FABRIC-SVC] ERROR: Invalid service or name\n");
        return -1;
    }
    
    kputs("[FABRIC-SVC] Service name: ");
    kputs(service->name);
    kputs("\n");
    
    fabric_log("[fabric] Publishing service: %s\n", service->name);
    kputs("[FABRIC-SVC] Acquiring lock...\n");
    spinlock_lock(&fabric_lock);
    kputs("[FABRIC-SVC] Lock acquired\n");
    
    if (service_count >= MAX_SERVICES) {
        spinlock_unlock(&fabric_lock);
        fabric_log("[fabric] ERROR: Service registry full\n");
        kputs("[FABRIC-SVC] ERROR: Service registry full\n");
        return -1;
    }
    
    kputs("[FABRIC-SVC] Adding service to registry...\n");
    service_registry[service_count++] = service;
    kputs("[FABRIC-SVC] Service added to registry\n");
    
    spinlock_unlock(&fabric_lock);
    kputs("[FABRIC-SVC] Lock released\n");
    
    fabric_log("[fabric] service published: %s (count=%u)\n", service->name, service_count);
    kputs("[FABRIC-SVC] fabric_service_publish() returning 0\n");
    
    return 0;
}

/* Lookup a service */
fabric_service_t* fabric_service_lookup(const char *name)
{
    if (!name) {
        return NULL;
    }
    
    spinlock_lock(&fabric_lock);
    
    for (uint32_t i = 0; i < service_count; i++) {
        if (service_registry[i] && service_registry[i]->name) {
            /* Simple string comparison */
            const char *s1 = service_registry[i]->name;
            const char *s2 = name;
            bool match = true;
            while (*s1 && *s2) {
                if (*s1 != *s2) {
                    match = false;
                    break;
                }
                s1++;
                s2++;
            }
            if (match && *s1 == *s2) {
                fabric_service_t *result = service_registry[i];
                spinlock_unlock(&fabric_lock);
                return result;
            }
        }
    }
    
    spinlock_unlock(&fabric_lock);
    return NULL;
}

/* Request IRQ */
int fabric_request_irq(int vector, fabric_irq_handler_t h, void *arg)
{
    extern void kprintf(const char* fmt, ...);
    
    if (!h || vector < 0 || vector >= 256) {
        kprintf("[FABRIC-IRQ] ERROR: Invalid parameters (vector=%d, handler=%p)\n", vector, h);
        return -1;
    }
    
    kprintf("[FABRIC-IRQ] Requesting IRQ: vector=%d\n", vector);
    
    spinlock_lock(&irq_lock);
    
    /* Find free slot */
    uint32_t slot = MAX_IRQ_HANDLERS;
    for (uint32_t i = 0; i < MAX_IRQ_HANDLERS; i++) {
        if (!irq_handlers[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot >= MAX_IRQ_HANDLERS) {
        spinlock_unlock(&irq_lock);
        kputs("[FABRIC-IRQ] ERROR: No free slots\n");
        return -1;
    }
    
    irq_handlers[slot].vector = vector;
    irq_handlers[slot].handler = h;
    irq_handlers[slot].arg = arg;
    irq_handlers[slot].active = true;
    
    spinlock_unlock(&irq_lock);
    
    kprintf("[FABRIC-IRQ] Handler registered in slot %u\n", slot);
    
    /* Register with interrupt system */
    extern int interrupt_register(uint32_t vector, interrupt_handler_t handler);
    kprintf("[FABRIC-IRQ] Registering with interrupt system...\n");
    if (interrupt_register(vector, fabric_irq_wrapper) != 0) {
        kputs("[FABRIC-IRQ] ERROR: Failed to register with interrupt system\n");
        spinlock_lock(&irq_lock);
        irq_handlers[slot].active = false;
        spinlock_unlock(&irq_lock);
        return -1;
    }
    
    kprintf("[FABRIC-IRQ] Successfully registered IRQ handler for vector %d\n", vector);
    return 0;
}

/* Free IRQ */
void fabric_free_irq(int vector, fabric_irq_handler_t h)
{
    if (!h || vector < 0 || vector >= 256) {
        return;
    }
    
    spinlock_lock(&irq_lock);
    
    for (uint32_t i = 0; i < MAX_IRQ_HANDLERS; i++) {
        if (irq_handlers[i].active && 
            irq_handlers[i].vector == vector && 
            irq_handlers[i].handler == h) {
            irq_handlers[i].active = false;
            break;
        }
    }
    
    spinlock_unlock(&irq_lock);
}

/* Logging */
void fabric_log(const char *fmt, ...)
{
    /* XNU-style: Logging with variadic arguments */
    /* Forward variadic arguments to kprintf using va_list */
    extern void kvprintf(const char *fmt, va_list args);
    
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}

