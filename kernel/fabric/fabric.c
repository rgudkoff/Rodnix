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
#include "bus/pci.h"
#include "device/device.h"
#include "driver/driver.h"
#include "service/service.h"
#include "../../include/console.h"
#include "../../include/common.h"
#include "../../core/interrupts.h"
#include "../../include/error.h"
#include <stddef.h>
#include <stdarg.h>

/* Maximum number of buses, drivers, devices, services */
#define MAX_BUSES    16
#define MAX_DRIVERS  64
#define MAX_DEVICES  256
#define MAX_SERVICES 64
#define MAX_NODES    512
#define MAX_EVENT_LISTENERS 16
#define MAX_EVENT_QUEUE 128

/* Registries */
static fabric_bus_t* bus_registry[MAX_BUSES];
static fabric_driver_t* driver_registry[MAX_DRIVERS];
static fabric_device_t* device_registry[MAX_DEVICES];
static fabric_service_t* service_registry[MAX_SERVICES];
static int32_t device_node_index[MAX_DEVICES];

typedef struct fabric_node_entry {
    bool used;
    uint64_t id;
    char path[FABRIC_NODE_PATH_MAX];
    char name[FABRIC_NODE_NAME_MAX];
    char type[FABRIC_NODE_TYPE_MAX];
    char class_name[FABRIC_NODE_CLASS_MAX];
    char provider_path[FABRIC_NODE_PATH_MAX];
    char driver[FABRIC_NODE_NAME_MAX];
    uint32_t state;
    uint32_t flags;
    void* provider;
    int32_t parent_index;
} fabric_node_entry_t;

static fabric_node_entry_t node_registry[MAX_NODES];
static uint32_t node_count = 0;
static uint64_t next_node_id = 1;

static uint32_t bus_count = 0;
static uint32_t driver_count = 0;
static uint32_t device_count = 0;
static uint32_t service_count = 0;
static bool fabric_initialized = false;

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

typedef struct fabric_event_listener {
    bool active;
    fabric_event_handler_t handler;
    void* arg;
} fabric_event_listener_t;

static fabric_event_listener_t event_listeners[MAX_EVENT_LISTENERS];
static fabric_event_t event_queue[MAX_EVENT_QUEUE];
static uint32_t event_head = 0;
static uint32_t event_count = 0;
static uint32_t event_dropped = 0;
static uint64_t event_seq = 1;

static void f_strcpy(char* dst, uint32_t cap, const char* src)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    uint32_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool f_starts_with(const char* s, const char* prefix)
{
    if (!s || !prefix) {
        return false;
    }
    while (*prefix) {
        if (*s != *prefix) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

static void f_append(char* dst, uint32_t cap, const char* src)
{
    if (!dst || cap == 0 || !src) {
        return;
    }
    uint32_t i = 0;
    while (i + 1 < cap && dst[i]) {
        i++;
    }
    uint32_t j = 0;
    while (i + 1 < cap && src[j]) {
        dst[i++] = src[j++];
    }
    dst[i] = '\0';
}

static char f_hex_digit(uint8_t v)
{
    v &= 0x0Fu;
    return (v < 10u) ? (char)('0' + v) : (char)('A' + (v - 10u));
}

static const char* fabric_state_name(uint32_t state)
{
    switch (state) {
        case FABRIC_STATE_INIT: return "init";
        case FABRIC_STATE_DISCOVERED: return "discovered";
        case FABRIC_STATE_ATTACHED: return "attached";
        case FABRIC_STATE_PUBLISHED: return "published";
        case FABRIC_STATE_REGISTERED: return "registered";
        case FABRIC_STATE_ACTIVE: return "active";
        case FABRIC_STATE_ERROR: return "error";
        case FABRIC_STATE_REMOVED: return "removed";
        default: return "unspec";
    }
}

static int32_t fabric_node_find_path_locked(const char* path)
{
    if (!path) {
        return -1;
    }
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        if (node_registry[i].used && strcmp(node_registry[i].path, path) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t fabric_node_find_provider_locked(void* provider)
{
    if (!provider) {
        return -1;
    }
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        if (node_registry[i].used && node_registry[i].provider == provider) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t fabric_node_add_locked(const char* path,
                                      const char* name,
                                      const char* type,
                                      const char* class_name,
                                      int32_t parent_index,
                                      void* provider,
                                      uint32_t state,
                                      uint32_t flags,
                                      const char* provider_path)
{
    int32_t existing = fabric_node_find_path_locked(path);
    if (existing >= 0) {
        if (name && name[0]) {
            f_strcpy(node_registry[existing].name, FABRIC_NODE_NAME_MAX, name);
        }
        if (type && type[0]) {
            f_strcpy(node_registry[existing].type, FABRIC_NODE_TYPE_MAX, type);
        }
        if (class_name && class_name[0]) {
            f_strcpy(node_registry[existing].class_name, FABRIC_NODE_CLASS_MAX, class_name);
        }
        if (provider) {
            node_registry[existing].provider = provider;
        }
        if (provider_path && provider_path[0]) {
            f_strcpy(node_registry[existing].provider_path, FABRIC_NODE_PATH_MAX, provider_path);
        }
        if (state != FABRIC_STATE_UNSPEC) {
            node_registry[existing].state = state;
        }
        node_registry[existing].flags |= flags;
        return existing;
    }
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        if (!node_registry[i].used) {
            node_registry[i].used = true;
            node_registry[i].id = next_node_id++;
            f_strcpy(node_registry[i].path, FABRIC_NODE_PATH_MAX, path);
            f_strcpy(node_registry[i].name, FABRIC_NODE_NAME_MAX, name);
            f_strcpy(node_registry[i].type, FABRIC_NODE_TYPE_MAX, type);
            f_strcpy(node_registry[i].class_name, FABRIC_NODE_CLASS_MAX, class_name);
            f_strcpy(node_registry[i].provider_path, FABRIC_NODE_PATH_MAX, provider_path);
            node_registry[i].driver[0] = '\0';
            node_registry[i].state = state;
            node_registry[i].flags = flags;
            node_registry[i].provider = provider;
            node_registry[i].parent_index = parent_index;
            node_count++;
            return (int32_t)i;
        }
    }
    return -1;
}

static void fabric_node_bootstrap_locked(void)
{
    int32_t root = fabric_node_add_locked("/fabric", "fabric", "root", "fabric", -1, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    int32_t devices = fabric_node_add_locked("/fabric/devices", "devices", "scope", "device", root, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    (void)fabric_node_add_locked("/fabric/devices/pci0", "pci0", "bus", "pci", devices, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    (void)fabric_node_add_locked("/fabric/devices/platform", "platform", "bus", "platform", devices, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    (void)fabric_node_add_locked("/fabric/devices/virtual", "virtual", "bus", "virtual", devices, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    (void)fabric_node_add_locked("/fabric/devices/unknown", "unknown", "bus", "device", devices, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    (void)fabric_node_add_locked("/fabric/services", "services", "scope", "service", root, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    int32_t subsystems = fabric_node_add_locked("/fabric/subsystems", "subsystems", "scope", "subsystem", root, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    (void)fabric_node_add_locked("/fabric/subsystems/net", "net", "subsystem", "net", subsystems, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    (void)fabric_node_add_locked("/fabric/subsystems/input", "input", "subsystem", "input", subsystems, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
    (void)fabric_node_add_locked("/fabric/subsystems/display", "display", "subsystem", "display", subsystems, NULL, FABRIC_STATE_ACTIVE, 0, NULL);
}

static const char* fabric_device_label(const fabric_device_t* dev)
{
    if (!dev) {
        return "device";
    }
    if (dev->class_code == 0x06 && dev->subclass == 0x00) return "host-bridge";
    if (dev->class_code == 0x06 && dev->subclass == 0x01) return "isa-bridge";
    if (dev->class_code == 0x01 && dev->subclass == 0x01) return "ide-controller";
    if (dev->class_code == 0x06 && dev->subclass == 0x80) return "acpi-pm";
    if (dev->class_code == 0x03 && dev->subclass == 0x00) return "vga-adapter";
    if (dev->class_code == 0x02 && dev->subclass == 0x00 && dev->vendor_id == 0x8086u) return "e1000-nic";
    if (dev->class_code == 0x02) return "nic";
    if (dev->class_code == 0x03 && dev->subclass == 0x01) return "keyboard";
    return dev->name ? dev->name : "device";
}

static void fabric_build_pci_path(char* out, uint32_t cap, const pci_device_info_t* pci)
{
    if (!out || cap == 0 || !pci) {
        return;
    }
    f_strcpy(out, cap, "/fabric/devices/pci0/");
    uint8_t b = pci->bus;
    uint8_t d = pci->device;
    uint8_t f = pci->function;
    char tail[8];
    tail[0] = f_hex_digit((uint8_t)(b >> 4));
    tail[1] = f_hex_digit((uint8_t)b);
    tail[2] = ':';
    tail[3] = f_hex_digit((uint8_t)(d >> 4));
    tail[4] = f_hex_digit((uint8_t)d);
    tail[5] = '.';
    tail[6] = f_hex_digit(f);
    tail[7] = '\0';
    f_append(out, cap, tail);
}

static void fabric_node_set_driver_locked(fabric_device_t* dev, const char* driver_name)
{
    int32_t node_idx = fabric_node_find_provider_locked(dev);
    if (node_idx < 0) {
        return;
    }
    f_strcpy(node_registry[node_idx].driver, FABRIC_NODE_NAME_MAX, driver_name);
    node_registry[node_idx].state = FABRIC_STATE_ATTACHED;
    node_registry[node_idx].flags |= 1u; /* bound */
}

static void fabric_node_path_for_provider_locked(void* provider, char* out, uint32_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    int32_t node_idx = fabric_node_find_provider_locked(provider);
    if (node_idx >= 0) {
        f_strcpy(out, cap, node_registry[node_idx].path);
    }
}

static void fabric_event_emit(uint32_t type,
                              const char* node_path,
                              const char* subject,
                              const char* detail,
                              uint32_t flags)
{
    fabric_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.flags = flags;
    f_strcpy(ev.node_path, sizeof(ev.node_path), node_path);
    f_strcpy(ev.subject, sizeof(ev.subject), subject);
    f_strcpy(ev.detail, sizeof(ev.detail), detail);

    fabric_event_listener_t listeners[MAX_EVENT_LISTENERS];
    uint32_t listener_count = 0;

    spinlock_lock(&fabric_lock);
    ev.seq = event_seq++;
    uint32_t slot = (event_head + event_count) % MAX_EVENT_QUEUE;
    if (event_count == MAX_EVENT_QUEUE) {
        event_head = (event_head + 1) % MAX_EVENT_QUEUE;
        event_dropped++;
        slot = (event_head + event_count - 1) % MAX_EVENT_QUEUE;
    } else {
        event_count++;
    }
    event_queue[slot] = ev;

    for (uint32_t i = 0; i < MAX_EVENT_LISTENERS; i++) {
        if (event_listeners[i].active && event_listeners[i].handler) {
            listeners[listener_count++] = event_listeners[i];
        }
    }
    spinlock_unlock(&fabric_lock);

    for (uint32_t i = 0; i < listener_count; i++) {
        listeners[i].handler(&ev, listeners[i].arg);
    }
}

/* Internal IRQ handler wrapper */
static void fabric_irq_wrapper(interrupt_context_t* ctx)
{
    int vector = ctx->vector;
    
    /* Call all registered handlers for this vector */
    /* Avoid any operations that might use FPU */
    /* Use simple integer operations only */
    uint32_t i = 0;
    uint32_t handler_count = 0;
    while (i < MAX_IRQ_HANDLERS) {
        if (irq_handlers[i].active && irq_handlers[i].vector == vector) {
            irq_handlers[i].handler(vector, irq_handlers[i].arg);
            handler_count++;
        }
        i++; /* Increment manually to avoid potential FPU usage */
    }
    
    (void)handler_count;
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
    for (uint32_t i = 0; i < MAX_DEVICES; i++) {
        device_node_index[i] = -1;
    }
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        node_registry[i].used = false;
    }
    for (uint32_t i = 0; i < MAX_IRQ_HANDLERS; i++) {
        irq_handlers[i].active = false;
    }
    for (uint32_t i = 0; i < MAX_EVENT_LISTENERS; i++) {
        event_listeners[i].active = false;
        event_listeners[i].handler = NULL;
        event_listeners[i].arg = NULL;
    }
    event_head = 0;
    event_count = 0;
    event_dropped = 0;
    event_seq = 1;
    
    bus_count = 0;
    driver_count = 0;
    device_count = 0;
    service_count = 0;
    node_count = 0;
    next_node_id = 1;
    fabric_node_bootstrap_locked();
    
    fabric_log("[fabric] Fabric initialized\n");
    fabric_initialized = true;
}

int fabric_get_stats(fabric_stats_t* out)
{
    if (!out) {
        return RDNX_E_INVALID;
    }
    out->buses = bus_count;
    out->drivers = driver_count;
    out->devices = device_count;
    out->services = service_count;
    return RDNX_OK;
}

uint32_t fabric_device_count(void)
{
    uint32_t count = 0;
    spinlock_lock(&fabric_lock);
    count = device_count;
    spinlock_unlock(&fabric_lock);
    return count;
}

fabric_device_t* fabric_device_get(uint32_t index)
{
    fabric_device_t* dev = NULL;
    spinlock_lock(&fabric_lock);
    if (index < device_count) {
        dev = device_registry[index];
    }
    spinlock_unlock(&fabric_lock);
    return dev;
}

uint32_t fabric_node_count(void)
{
    uint32_t count = 0;
    spinlock_lock(&fabric_lock);
    count = node_count;
    spinlock_unlock(&fabric_lock);
    return count;
}

int fabric_node_get_info(uint32_t index, fabric_node_info_t* out)
{
    if (!out) {
        return RDNX_E_INVALID;
    }
    memset(out, 0, sizeof(*out));
    spinlock_lock(&fabric_lock);
    uint32_t seen = 0;
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        if (!node_registry[i].used) {
            continue;
        }
        if (seen == index) {
            out->id = node_registry[i].id;
            memcpy(out->path, node_registry[i].path, sizeof(out->path));
            memcpy(out->name, node_registry[i].name, sizeof(out->name));
            memcpy(out->type, node_registry[i].type, sizeof(out->type));
            memcpy(out->class_name, node_registry[i].class_name, sizeof(out->class_name));
            memcpy(out->provider_path, node_registry[i].provider_path, sizeof(out->provider_path));
            memcpy(out->driver, node_registry[i].driver, sizeof(out->driver));
            out->state = node_registry[i].state;
            out->flags = node_registry[i].flags;
            spinlock_unlock(&fabric_lock);
            return RDNX_OK;
        }
        seen++;
    }
    spinlock_unlock(&fabric_lock);
    return RDNX_E_NOTFOUND;
}

int fabric_node_list(fabric_node_info_t* out, uint32_t max_entries, uint32_t* out_total)
{
    if (!out || max_entries == 0) {
        return RDNX_E_INVALID;
    }
    spinlock_lock(&fabric_lock);
    uint32_t total = node_count;
    uint32_t n = (total < max_entries) ? total : max_entries;
    uint32_t wr = 0;
    for (uint32_t i = 0; i < MAX_NODES && wr < n; i++) {
        if (!node_registry[i].used) {
            continue;
        }
        memset(&out[wr], 0, sizeof(out[wr]));
        out[wr].id = node_registry[i].id;
        memcpy(out[wr].path, node_registry[i].path, sizeof(out[wr].path));
        memcpy(out[wr].name, node_registry[i].name, sizeof(out[wr].name));
        memcpy(out[wr].type, node_registry[i].type, sizeof(out[wr].type));
        memcpy(out[wr].class_name, node_registry[i].class_name, sizeof(out[wr].class_name));
        memcpy(out[wr].provider_path, node_registry[i].provider_path, sizeof(out[wr].provider_path));
        memcpy(out[wr].driver, node_registry[i].driver, sizeof(out[wr].driver));
        out[wr].state = node_registry[i].state;
        out[wr].flags = node_registry[i].flags;
        wr++;
    }
    spinlock_unlock(&fabric_lock);
    if (out_total) {
        *out_total = total;
    }
    return (int)wr;
}

int fabric_event_subscribe(fabric_event_handler_t handler, void* arg)
{
    if (!handler) {
        return RDNX_E_INVALID;
    }
    spinlock_lock(&fabric_lock);
    for (uint32_t i = 0; i < MAX_EVENT_LISTENERS; i++) {
        if (!event_listeners[i].active) {
            event_listeners[i].active = true;
            event_listeners[i].handler = handler;
            event_listeners[i].arg = arg;
            spinlock_unlock(&fabric_lock);
            return RDNX_OK;
        }
    }
    spinlock_unlock(&fabric_lock);
    return RDNX_E_BUSY;
}

int fabric_node_set_state(const char* path, uint32_t state)
{
    if (!path || state == FABRIC_STATE_UNSPEC) {
        return RDNX_E_INVALID;
    }
    bool changed = false;
    spinlock_lock(&fabric_lock);
    int32_t node_idx = fabric_node_find_path_locked(path);
    if (node_idx < 0) {
        spinlock_unlock(&fabric_lock);
        return RDNX_E_NOTFOUND;
    }
    if (node_registry[node_idx].state != state) {
        node_registry[node_idx].state = state;
        changed = true;
    }
    spinlock_unlock(&fabric_lock);
    if (changed) {
        fabric_event_emit(FABRIC_EVENT_STATE_CHANGED,
                         path,
                         "",
                         fabric_state_name(state),
                         0);
    }
    return RDNX_OK;
}

void fabric_event_unsubscribe(fabric_event_handler_t handler, void* arg)
{
    if (!handler) {
        return;
    }
    spinlock_lock(&fabric_lock);
    for (uint32_t i = 0; i < MAX_EVENT_LISTENERS; i++) {
        if (event_listeners[i].active &&
            event_listeners[i].handler == handler &&
            event_listeners[i].arg == arg) {
            event_listeners[i].active = false;
            event_listeners[i].handler = NULL;
            event_listeners[i].arg = NULL;
            break;
        }
    }
    spinlock_unlock(&fabric_lock);
}

int fabric_event_drain(fabric_event_t* out, uint32_t max_entries, uint32_t* out_read, uint32_t* out_dropped)
{
    if (!out || max_entries == 0) {
        return RDNX_E_INVALID;
    }
    spinlock_lock(&fabric_lock);
    uint32_t n = (event_count < max_entries) ? event_count : max_entries;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (event_head + i) % MAX_EVENT_QUEUE;
        out[i] = event_queue[idx];
    }
    event_head = (event_head + n) % MAX_EVENT_QUEUE;
    event_count -= n;
    uint32_t dropped = event_dropped;
    event_dropped = 0;
    spinlock_unlock(&fabric_lock);

    if (out_read) {
        *out_read = n;
    }
    if (out_dropped) {
        *out_dropped = dropped;
    }
    return RDNX_OK;
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
                fabric_node_set_driver_locked(dev, driver->name);
                char node_path[FABRIC_NODE_PATH_MAX];
                fabric_node_path_for_provider_locked(dev, node_path, sizeof(node_path));
                spinlock_unlock(&fabric_lock);
                kputs("[FABRIC] Device marked as attached, lock released\n");
                fabric_log("[fabric] driver attached: %s -> %s\n", 
                         driver->name, dev->name);
                fabric_event_emit(FABRIC_EVENT_DRIVER_ATTACHED,
                                 node_path,
                                 dev->name ? dev->name : "device",
                                 driver->name,
                                 0);
                if (driver->publish) {
                    (void)driver->publish(dev);
                }
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
    
    uint32_t dev_index = device_count;
    device_registry[device_count++] = device;
    device->driver_state = NULL; /* No driver yet */

    char path[FABRIC_NODE_PATH_MAX];
    const char* class_name = "device";
    const char* label = fabric_device_label(device);
    int32_t parent_idx = fabric_node_find_path_locked("/fabric/devices/unknown");
    f_strcpy(path, sizeof(path), "/fabric/devices/unknown/dev");
    char idx_tail[5];
    idx_tail[0] = f_hex_digit((uint8_t)((dev_index >> 4) & 0x0F));
    idx_tail[1] = f_hex_digit((uint8_t)(dev_index & 0x0F));
    idx_tail[2] = '\0';
    f_append(path, sizeof(path), idx_tail);

    if (strcmp(device->name, "pci-device") == 0 && device->bus_private) {
        const pci_device_info_t* pci = (const pci_device_info_t*)device->bus_private;
        fabric_build_pci_path(path, sizeof(path), pci);
        class_name = "pci";
        parent_idx = fabric_node_find_path_locked("/fabric/devices/pci0");
    } else if (f_starts_with(device->name, "ps2-")) {
        f_strcpy(path, sizeof(path), "/fabric/devices/platform/ps2kbd0");
        class_name = "input";
        parent_idx = fabric_node_find_path_locked("/fabric/devices/platform");
    } else if (f_starts_with(device->name, "virt-")) {
        f_strcpy(path, sizeof(path), "/fabric/devices/virtual/dummy0");
        class_name = "virtual";
        parent_idx = fabric_node_find_path_locked("/fabric/devices/virtual");
    }

    int32_t node_idx = fabric_node_add_locked(path,
                                              label,
                                              "device",
                                              class_name,
                                              parent_idx,
                                              device,
                                              FABRIC_STATE_DISCOVERED,
                                              0,
                                              NULL);
    device_node_index[dev_index] = node_idx;
    
    spinlock_unlock(&fabric_lock);
    
    fabric_log("[fabric] device found: vendor=%x device=%x class=%x\n",
               device->vendor_id, device->device_id, device->class_code);
    fabric_event_emit(FABRIC_EVENT_DEVICE_ADDED,
                     path,
                     label,
                     device->name ? device->name : "device",
                     0);
    
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
                spinlock_lock(&fabric_lock);
                fabric_node_set_driver_locked(device, driver->name);
                char node_path[FABRIC_NODE_PATH_MAX];
                fabric_node_path_for_provider_locked(device, node_path, sizeof(node_path));
                spinlock_unlock(&fabric_lock);
                fabric_log("[fabric] driver attached: %s -> %s\n", 
                         driver->name, device->name);
                fabric_event_emit(FABRIC_EVENT_DRIVER_ATTACHED,
                                 node_path,
                                 device->name ? device->name : "device",
                                 driver->name,
                                 0);
                if (driver->publish) {
                    (void)driver->publish(device);
                }
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
        return RDNX_E_INVALID;
    }

    if (service->hdr.abi_version != RDNX_ABI_VERSION ||
        service->hdr.size < sizeof(fabric_service_t)) {
        kputs("[FABRIC-SVC] ERROR: Service ABI mismatch\n");
        return RDNX_E_INVALID;
    }
    if (service->ops) {
        rdnx_abi_header_t* ops_hdr = (rdnx_abi_header_t*)service->ops;
        if (ops_hdr->abi_version != RDNX_ABI_VERSION ||
            ops_hdr->size < sizeof(rdnx_abi_header_t)) {
            kputs("[FABRIC-SVC] ERROR: Service ops ABI mismatch\n");
            return RDNX_E_INVALID;
        }
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

    char path[FABRIC_NODE_PATH_MAX];
    int32_t parent_idx = -1;
    uint32_t node_flags = 0;
    const char* node_type = "service";
    const char* node_class = "service";
    if (strcmp(service->name, "net.ifmgr") == 0) {
        f_strcpy(path, sizeof(path), "/fabric/subsystems/net/ifmgr");
        parent_idx = fabric_node_find_path_locked("/fabric/subsystems/net");
        node_type = "manager";
        node_class = "net";
        node_flags = 0x2u; /* internal */
    } else {
        f_strcpy(path, sizeof(path), "/fabric/services/");
        f_append(path, sizeof(path), service->name);
        parent_idx = fabric_node_find_path_locked("/fabric/services");
    }
    (void)fabric_node_add_locked(path,
                                 service->name,
                                 node_type,
                                 node_class,
                                 parent_idx,
                                 service,
                                 FABRIC_STATE_PUBLISHED,
                                 node_flags,
                                 NULL);
    
    spinlock_unlock(&fabric_lock);
    kputs("[FABRIC-SVC] Lock released\n");
    fabric_event_emit(FABRIC_EVENT_SERVICE_PUBLISHED,
                     path,
                     service->name,
                     node_class,
                     node_flags);
    
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

int fabric_publish_netif_node(const char* ifname, fabric_device_t* provider_dev)
{
    if (!ifname || !provider_dev) {
        return RDNX_E_INVALID;
    }
    char path[FABRIC_NODE_PATH_MAX];
    f_strcpy(path, sizeof(path), "/fabric/services/");
    f_append(path, sizeof(path), ifname);

    char provider_path[FABRIC_NODE_PATH_MAX];
    provider_path[0] = '\0';

    spinlock_lock(&fabric_lock);
    bool existed = (fabric_node_find_path_locked(path) >= 0);
    int32_t parent_idx = fabric_node_find_path_locked("/fabric/services");
    int32_t provider_idx = fabric_node_find_provider_locked(provider_dev);
    if (provider_idx >= 0) {
        f_strcpy(provider_path, sizeof(provider_path), node_registry[provider_idx].path);
    }
    int32_t node_idx = fabric_node_add_locked(path,
                                              ifname,
                                              "service",
                                              "net",
                                              parent_idx,
                                              provider_dev,
                                              existed ? FABRIC_STATE_REGISTERED : FABRIC_STATE_PUBLISHED,
                                              0,
                                              provider_path);
    spinlock_unlock(&fabric_lock);
    if (node_idx >= 0) {
        if (!existed) {
            fabric_event_emit(FABRIC_EVENT_SERVICE_PUBLISHED,
                             path,
                             ifname,
                             "net",
                             0);
        }
        (void)fabric_node_set_state(path, FABRIC_STATE_REGISTERED);
        fabric_event_emit(FABRIC_EVENT_SERVICE_REGISTERED,
                         path,
                         ifname,
                         "net",
                         0);
    }
    return (node_idx >= 0) ? RDNX_OK : RDNX_E_BUSY;
}

int fabric_publish_input_node(const char* input_name, fabric_device_t* provider_dev)
{
    if (!input_name || !provider_dev) {
        return RDNX_E_INVALID;
    }
    char path[FABRIC_NODE_PATH_MAX];
    f_strcpy(path, sizeof(path), "/fabric/services/");
    f_append(path, sizeof(path), input_name);

    char provider_path[FABRIC_NODE_PATH_MAX];
    provider_path[0] = '\0';

    spinlock_lock(&fabric_lock);
    bool existed = (fabric_node_find_path_locked(path) >= 0);
    int32_t parent_idx = fabric_node_find_path_locked("/fabric/services");
    int32_t provider_idx = fabric_node_find_provider_locked(provider_dev);
    if (provider_idx >= 0) {
        f_strcpy(provider_path, sizeof(provider_path), node_registry[provider_idx].path);
    }
    int32_t node_idx = fabric_node_add_locked(path,
                                              input_name,
                                              "service",
                                              "input",
                                              parent_idx,
                                              provider_dev,
                                              existed ? FABRIC_STATE_REGISTERED : FABRIC_STATE_PUBLISHED,
                                              0,
                                              provider_path);
    spinlock_unlock(&fabric_lock);
    if (node_idx >= 0) {
        if (!existed) {
            fabric_event_emit(FABRIC_EVENT_SERVICE_PUBLISHED,
                             path,
                             input_name,
                             "input",
                             0);
        }
        (void)fabric_node_set_state(path, FABRIC_STATE_REGISTERED);
        fabric_event_emit(FABRIC_EVENT_SERVICE_REGISTERED,
                         path,
                         input_name,
                         "input",
                         0);
    }
    return (node_idx >= 0) ? RDNX_OK : RDNX_E_BUSY;
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
    /* Logging with variadic arguments */
    /* Forward variadic arguments to kprintf using va_list */
    extern void kvprintf(const char *fmt, va_list args);
    
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
