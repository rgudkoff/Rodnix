/**
 * @file usb_hid.c
 * @brief USB HID keyboard driver (boot protocol)
 */

#include "usb_hid.h"
#include "../../../kernel/usb/usb_proto.h"
#include "../../../kernel/usb/usb.h"
#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/input/input.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include "../../../trace/bootlog.h"
#include <stdbool.h>
#include <stdint.h>

#define HID_KBD_REPORT_SIZE 8u
#define USB_HID_SLOT_MAX    2u

/* HID keycode → PS/2 set-1 scancode (0 = unmapped) */
static const uint8_t g_hid_to_ps2[128u] = {
    0x00, 0x00, 0x00, 0x00, 0x1E, 0x30, 0x2E, 0x20,  /* 00-07 */
    0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,  /* 08-0F */
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,  /* 10-17 */
    0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x02, 0x03,  /* 18-1F */
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,  /* 20-27 */
    0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A,  /* 28-2F */
    0x1B, 0x2B, 0x00, 0x27, 0x28, 0x29, 0x33, 0x34,  /* 30-37 */
    0x35, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,  /* 38-3F */
    0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0x00, 0x46,  /* 40-47 */
    0x00, 0x52, 0x47, 0x49, 0x53, 0x4F, 0x51, 0x4D,  /* 48-4F */
    0x4B, 0x50, 0x48, 0x45, 0x35, 0x37, 0x4A, 0x4E,  /* 50-57 */
    0x1C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47,  /* 58-5F */
    0x48, 0x49, 0x52, 0x53, 0x56, 0x00, 0x00, 0x59,  /* 60-67 */
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B,  /* 68-6F */
    0x6C, 0x6D, 0x6E, 0x76, 0x00, 0x00, 0x00, 0x00,  /* 70-77 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   /* 78-7F */
};

typedef struct {
    int used;
    fabric_device_t* dev;
    usb_host_controller_t* host;
    usb_port_device_info_t* port_info;
    usb_ep_handle_t intr_ep;
    uint8_t prev_report[HID_KBD_REPORT_SIZE];
} usb_hid_slot_t;

static usb_hid_slot_t g_hid_slots[USB_HID_SLOT_MAX];
static uint32_t g_hid_count = 0u;

static void usb_hid_process_report(usb_hid_slot_t* hid, const uint8_t* report)
{
    uint8_t i;
    uint8_t j;
    uint8_t keycode;
    uint8_t ps2;
    bool found;

    for (i = 2u; i < HID_KBD_REPORT_SIZE; i++) {
        keycode = hid->prev_report[i];
        if (keycode == 0u) {
            continue;
        }
        found = false;
        for (j = 2u; j < HID_KBD_REPORT_SIZE; j++) {
            if (report[j] == keycode) {
                found = true;
                break;
            }
        }
        if (!found && keycode < 128u) {
            ps2 = g_hid_to_ps2[keycode];
            if (ps2 != 0u) {
                input_push_scancode(ps2, false);
            }
        }
    }

    for (i = 2u; i < HID_KBD_REPORT_SIZE; i++) {
        keycode = report[i];
        if (keycode == 0u) {
            continue;
        }
        found = false;
        for (j = 2u; j < HID_KBD_REPORT_SIZE; j++) {
            if (hid->prev_report[j] == keycode) {
                found = true;
                break;
            }
        }
        if (!found && keycode < 128u) {
            ps2 = g_hid_to_ps2[keycode];
            if (ps2 != 0u) {
                input_push_scancode(ps2, true);
            }
        }
    }
}

static bool usb_hid_probe(fabric_device_t* dev)
{
    usb_port_device_info_t* info;

    if (!dev || !dev->bus_private) {
        return false;
    }
    info = (usb_port_device_info_t*)dev->bus_private;
    return (info->interface_class == USB_CLASS_HID &&
            info->interface_subclass == USB_HID_SUBCLASS_BOOT &&
            info->interface_protocol == USB_HID_PROTOCOL_KBD);
}

static int usb_hid_match_score(fabric_device_t* dev)
{
    return usb_hid_probe(dev) ? FABRIC_MATCH_DEVICE_EXACT : FABRIC_MATCH_NONE;
}

static int usb_hid_attach(fabric_device_t* dev)
{
    usb_hid_slot_t* hid;
    usb_host_controller_t* host;
    usb_port_device_info_t* info;
    usb_ep_handle_t intr_ep;
    uint32_t i;

    if (!dev || !dev->bus_private) {
        return RDNX_E_INVALID;
    }

    for (i = 0u; i < USB_HID_SLOT_MAX; i++) {
        if (!g_hid_slots[i].used) {
            break;
        }
    }
    if (i >= USB_HID_SLOT_MAX) {
        return RDNX_E_BUSY;
    }

    info = (usb_port_device_info_t*)dev->bus_private;
    host = info->host;
    if (!host || !host->ops || !host->ops->find_endpoint) {
        return RDNX_E_NOTFOUND;
    }

    intr_ep = host->ops->find_endpoint(host, info, USB_EP_TYPE_INTERRUPT, true);
    if (!intr_ep) {
        klog("usb-hid", "no interrupt IN endpoint for port%u\n", info->port_number);
        return RDNX_E_NOTFOUND;
    }

    hid = &g_hid_slots[i];
    memset(hid, 0, sizeof(*hid));
    hid->used = 1;
    hid->dev = dev;
    hid->host = host;
    hid->port_info = info;
    hid->intr_ep = intr_ep;
    g_hid_count++;

    klog("usb-hid", "keyboard attached: port%u\n", info->port_number);
    return RDNX_OK;
}

static int usb_hid_publish(fabric_device_t* dev)
{
    (void)dev;
    return RDNX_OK;
}

static void usb_hid_detach(fabric_device_t* dev)
{
    uint32_t i;
    for (i = 0u; i < USB_HID_SLOT_MAX; i++) {
        if (g_hid_slots[i].used && g_hid_slots[i].dev == dev) {
            g_hid_slots[i].used = 0;
            g_hid_count--;
            return;
        }
    }
}

static fabric_driver_t g_usb_hid_driver = {
    .name = "usb-hid-kbd",
    .match_properties = NULL,
    .match_property_count = 0,
    .match_score = usb_hid_match_score,
    .probe = usb_hid_probe,
    .attach = usb_hid_attach,
    .publish = usb_hid_publish,
    .detach = usb_hid_detach,
    .suspend = NULL,
    .resume = NULL
};

static void usb_hid_poll(void)
{
    uint32_t i;
    uint8_t report[HID_KBD_REPORT_SIZE];
    uint16_t actual;
    int rc;
    usb_host_controller_t* host;

    for (i = 0u; i < USB_HID_SLOT_MAX; i++) {
        if (!g_hid_slots[i].used || !g_hid_slots[i].intr_ep) {
            continue;
        }
        host = g_hid_slots[i].host;
        if (!host || !host->ops || !host->ops->poll_interrupt_in) {
            continue;
        }
        actual = 0u;
        rc = host->ops->poll_interrupt_in(host,
                                           g_hid_slots[i].port_info,
                                           g_hid_slots[i].intr_ep,
                                           report, HID_KBD_REPORT_SIZE, &actual);
        if (rc == RDNX_OK && actual > 0u) {
            usb_hid_process_report(&g_hid_slots[i], report);
            memcpy(g_hid_slots[i].prev_report, report, HID_KBD_REPORT_SIZE);
        }
    }
}

void usb_hid_init(void)
{
    int rc = fabric_driver_register(&g_usb_hid_driver);
    if (rc == RDNX_OK) {
        klog("usb-hid", "driver registered\n");
    } else {
        klog_warn("usb-hid", "driver registration failed\n");
    }
    usb_class_poll_register(usb_hid_poll);
}
