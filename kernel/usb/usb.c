/**
 * @file usb.c
 * @brief USB core helpers
 */

#include "usb.h"
#include "usb_proto.h"
#include "../fabric/spin.h"
#include "../../trace/bootlog.h"
#include "../../include/common.h"
#include "../../include/console.h"
#include "../../include/error.h"

#define USB_HOST_MAX        8u
#define USB_CLASS_POLL_MAX  8u

static spinlock_t g_usb_lock;
static usb_host_controller_t* g_usb_hosts[USB_HOST_MAX];
static usb_class_poll_fn_t g_class_polls[USB_CLASS_POLL_MAX];
static uint32_t g_class_poll_count = 0u;

void usb_init(void)
{
    spinlock_init(&g_usb_lock);
    for (uint32_t i = 0; i < USB_HOST_MAX; i++) {
        g_usb_hosts[i] = NULL;
    }
    klog("usb", "core initialized\n");
}

usb_host_type_t usb_host_type_from_prog_if(uint8_t prog_if)
{
    switch (prog_if) {
        case USB_PROGIF_UHCI:
            return USB_HOST_UHCI;
        case USB_PROGIF_OHCI:
            return USB_HOST_OHCI;
        case USB_PROGIF_EHCI:
            return USB_HOST_EHCI;
        case USB_PROGIF_XHCI:
            return USB_HOST_XHCI;
        default:
            return USB_HOST_UNKNOWN;
    }
}

const char* usb_host_type_name(usb_host_type_t type)
{
    switch (type) {
        case USB_HOST_UHCI:
            return "UHCI";
        case USB_HOST_OHCI:
            return "OHCI";
        case USB_HOST_EHCI:
            return "EHCI";
        case USB_HOST_XHCI:
            return "XHCI";
        default:
            return "USB";
    }
}

const char* usb_host_type_prefix(usb_host_type_t type)
{
    switch (type) {
        case USB_HOST_UHCI:
            return "uhci";
        case USB_HOST_OHCI:
            return "ohci";
        case USB_HOST_EHCI:
            return "ehci";
        case USB_HOST_XHCI:
            return "xhci";
        default:
            return "usb";
    }
}

int usb_host_register(usb_host_controller_t* host)
{
    if (!host || !host->name || host->name[0] == '\0') {
        return RDNX_E_INVALID;
    }

    spinlock_lock(&g_usb_lock);
    for (uint32_t i = 0; i < USB_HOST_MAX; i++) {
        if (g_usb_hosts[i] == host) {
            spinlock_unlock(&g_usb_lock);
            return RDNX_OK;
        }
        if (g_usb_hosts[i] && strcmp(g_usb_hosts[i]->name, host->name) == 0) {
            spinlock_unlock(&g_usb_lock);
            return RDNX_E_BUSY;
        }
    }

    for (uint32_t i = 0; i < USB_HOST_MAX; i++) {
        if (!g_usb_hosts[i]) {
            g_usb_hosts[i] = host;
            spinlock_unlock(&g_usb_lock);
            klog("usb", "host registered: %s (%s)\n",
                 host->name,
                 usb_host_type_name((usb_host_type_t)host->type));
            return RDNX_OK;
        }
    }

    spinlock_unlock(&g_usb_lock);
    return RDNX_E_BUSY;
}

int usb_host_unregister(usb_host_controller_t* host)
{
    if (!host) {
        return RDNX_E_INVALID;
    }

    spinlock_lock(&g_usb_lock);
    for (uint32_t i = 0; i < USB_HOST_MAX; i++) {
        if (g_usb_hosts[i] == host) {
            g_usb_hosts[i] = NULL;
            spinlock_unlock(&g_usb_lock);
            return RDNX_OK;
        }
    }
    spinlock_unlock(&g_usb_lock);
    return RDNX_E_NOTFOUND;
}

uint32_t usb_host_count(void)
{
    uint32_t count = 0;
    spinlock_lock(&g_usb_lock);
    for (uint32_t i = 0; i < USB_HOST_MAX; i++) {
        if (g_usb_hosts[i]) {
            count++;
        }
    }
    spinlock_unlock(&g_usb_lock);
    return count;
}

usb_host_controller_t* usb_host_get(uint32_t index)
{
    uint32_t seen = 0;
    usb_host_controller_t* host = NULL;

    spinlock_lock(&g_usb_lock);
    for (uint32_t i = 0; i < USB_HOST_MAX; i++) {
        if (!g_usb_hosts[i]) {
            continue;
        }
        if (seen == index) {
            host = g_usb_hosts[i];
            break;
        }
        seen++;
    }
    spinlock_unlock(&g_usb_lock);
    return host;
}

int usb_host_rescan(usb_host_controller_t* host)
{
    if (!host) {
        return RDNX_E_INVALID;
    }
    if (!host->ops || !host->ops->rescan) {
        return RDNX_E_UNSUPPORTED;
    }
    return host->ops->rescan(host);
}

void usb_class_poll_register(usb_class_poll_fn_t fn)
{
    if (!fn || g_class_poll_count >= USB_CLASS_POLL_MAX) {
        return;
    }
    g_class_polls[g_class_poll_count++] = fn;
}

void usb_class_poll_all(void)
{
    for (uint32_t i = 0u; i < g_class_poll_count; i++) {
        g_class_polls[i]();
    }
}

void usb_host_poll_all(void)
{
    usb_host_controller_t* snapshot[USB_HOST_MAX];
    uint32_t count = 0;

    spinlock_lock(&g_usb_lock);
    for (uint32_t i = 0; i < USB_HOST_MAX; i++) {
        if (g_usb_hosts[i]) {
            snapshot[count++] = g_usb_hosts[i];
        }
    }
    spinlock_unlock(&g_usb_lock);

    for (uint32_t i = 0; i < count; i++) {
        if (snapshot[i]->ops && snapshot[i]->ops->poll) {
            (void)snapshot[i]->ops->poll(snapshot[i]);
        }
    }
}

const char* usb_speed_name(uint8_t speed)
{
    switch (speed) {
        case 1u:
            return "full";
        case 2u:
            return "low";
        case 3u:
            return "high";
        case 4u:
            return "super";
        case 5u:
            return "superplus";
        default:
            return "unknown";
    }
}

const char* usb_device_state_name(uint8_t state)
{
    switch (state) {
        case USB_DEVICE_STATE_CONNECTED:
            return "connected";
        case USB_DEVICE_STATE_ENUM_PENDING:
            return "enum-pending";
        case USB_DEVICE_STATE_SLOT_ENABLED:
            return "slot-enabled";
        case USB_DEVICE_STATE_ADDRESSED:
            return "addressed";
        case USB_DEVICE_STATE_CONFIGURED:
            return "configured";
        case USB_DEVICE_STATE_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

const char* usb_request_name(uint8_t request)
{
    switch (request) {
        case USB_REQ_GET_STATUS:
            return "GET_STATUS";
        case USB_REQ_CLEAR_FEATURE:
            return "CLEAR_FEATURE";
        case USB_REQ_SET_FEATURE:
            return "SET_FEATURE";
        case USB_REQ_SET_ADDRESS:
            return "SET_ADDRESS";
        case USB_REQ_GET_DESCRIPTOR:
            return "GET_DESCRIPTOR";
        case USB_REQ_SET_DESCRIPTOR:
            return "SET_DESCRIPTOR";
        case USB_REQ_GET_CONFIGURATION:
            return "GET_CONFIGURATION";
        case USB_REQ_SET_CONFIGURATION:
            return "SET_CONFIGURATION";
        default:
            return "UNKNOWN";
    }
}

void usb_setup_packet_init(usb_setup_packet_t* pkt,
                           uint8_t request_type,
                           uint8_t request,
                           uint16_t value,
                           uint16_t index,
                           uint16_t length)
{
    if (!pkt) {
        return;
    }
    pkt->bmRequestType = request_type;
    pkt->bRequest = request;
    pkt->wValue = value;
    pkt->wIndex = index;
    pkt->wLength = length;
}

void usb_setup_get_descriptor(usb_setup_packet_t* pkt,
                              uint8_t desc_type,
                              uint8_t desc_index,
                              uint16_t lang_id,
                              uint16_t length)
{
    usb_setup_packet_init(pkt,
                          USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                          USB_REQ_GET_DESCRIPTOR,
                          (uint16_t)(((uint16_t)desc_type << 8) | desc_index),
                          lang_id,
                          length);
}

void usb_setup_set_address(usb_setup_packet_t* pkt, uint8_t address)
{
    usb_setup_packet_init(pkt,
                          USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                          USB_REQ_SET_ADDRESS,
                          address,
                          0u,
                          0u);
}

void usb_setup_set_configuration(usb_setup_packet_t* pkt, uint8_t config_value)
{
    usb_setup_packet_init(pkt,
                          USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                          USB_REQ_SET_CONFIGURATION,
                          config_value,
                          0u,
                          0u);
}
