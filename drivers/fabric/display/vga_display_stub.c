/**
 * @file vga_display_stub.c
 * @brief Fabric VGA display backend (MVP: detect + publish vga0/fb0)
 */

#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include <stdbool.h>
#include <stdint.h>

#define PCI_CLASS_DISPLAY 0x03u
#define PCI_SUBCLASS_VGA  0x00u
#define VGA_SLOT_MAX      2

typedef struct {
    int used;
    fabric_device_t* dev;
    const char* vga_name;
    const char* fb_name;
} vga_slot_t;

static vga_slot_t g_slots[VGA_SLOT_MAX];

static bool vga_display_probe(fabric_device_t* dev)
{
    if (!dev) {
        return false;
    }
    return (dev->class_code == PCI_CLASS_DISPLAY && dev->subclass == PCI_SUBCLASS_VGA);
}

static int vga_display_attach(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }
    for (uint32_t i = 0; i < VGA_SLOT_MAX; i++) {
        if (!g_slots[i].used) {
            g_slots[i].used = 1;
            g_slots[i].dev = dev;
            g_slots[i].vga_name = (i == 0) ? "vga0" : "vga1";
            g_slots[i].fb_name = (i == 0) ? "fb0" : "fb1";
            fabric_log("[VGA] attached %s vendor=%x device=%x\n",
                       g_slots[i].vga_name, dev->vendor_id, dev->device_id);
            return RDNX_OK;
        }
    }
    return RDNX_E_BUSY;
}

static int vga_display_publish(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }
    for (uint32_t i = 0; i < VGA_SLOT_MAX; i++) {
        if (!g_slots[i].used || g_slots[i].dev != dev) {
            continue;
        }
        if (fabric_publish_service_node(g_slots[i].vga_name, "display", dev) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
        if (fabric_publish_service_node(g_slots[i].fb_name, "display", dev) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
        return RDNX_OK;
    }
    return RDNX_E_NOTFOUND;
}

static void vga_display_detach(fabric_device_t* dev)
{
    (void)dev;
}

static fabric_driver_t g_driver = {
    .name = "vga-display-stub",
    .probe = vga_display_probe,
    .attach = vga_display_attach,
    .publish = vga_display_publish,
    .detach = vga_display_detach,
    .suspend = NULL,
    .resume = NULL
};

void vga_display_stub_init(void)
{
    int rc = fabric_driver_register(&g_driver);
    if (rc == RDNX_OK) {
        kputs("[VGA] driver registered\n");
    } else {
        kputs("[VGA] driver register failed\n");
    }
}
