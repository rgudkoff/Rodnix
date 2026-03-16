/**
 * @file usb_host_pci.c
 * @brief Fabric PCI USB host controller backend
 */

#include "usb_host_pci_internal.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include <stdint.h>

static usb_host_slot_t g_slots[USB_HOST_SLOT_MAX];
static uint32_t g_usb_host_count = 0;

static bool usb_host_pci_probe(fabric_device_t* dev)
{
    if (!dev) {
        return false;
    }
    return (dev->bus_private != NULL &&
            dev->name != NULL &&
            strcmp(dev->name, "pci-device") == 0 &&
            dev->class_code == USB_CLASS_SERIAL_BUS &&
            dev->subclass == USB_SUBCLASS_USB);
}

static int usb_host_pci_match_score(fabric_device_t* dev)
{
    if (!usb_host_pci_probe(dev)) {
        return FABRIC_MATCH_NONE;
    }
    return (usb_host_type_from_prog_if(dev->prog_if) != USB_HOST_UNKNOWN)
        ? FABRIC_MATCH_DEVICE_EXACT
        : FABRIC_MATCH_GENERIC;
}

static const fabric_property_t usb_host_pci_match_properties[] = {
    { .key = "bus", .type = FABRIC_PROP_STR, .value.str = "pci" },
    { .key = "class-code", .type = FABRIC_PROP_U32, .value.u32 = USB_CLASS_SERIAL_BUS },
    { .key = "subclass", .type = FABRIC_PROP_U32, .value.u32 = USB_SUBCLASS_USB },
};

static void usb_host_make_name(char* out, size_t out_len, usb_host_type_t type, uint32_t index)
{
    const char* prefix = usb_host_type_prefix(type);
    char digits[10];
    uint32_t digit_count = 0;
    size_t pos = 0;

    if (!out || out_len == 0) {
        return;
    }

    while (prefix[pos] != '\0' && pos + 1 < out_len) {
        out[pos] = prefix[pos];
        pos++;
    }
    if (pos + 1 >= out_len) {
        out[out_len - 1] = '\0';
        return;
    }

    do {
        digits[digit_count++] = (char)('0' + (index % 10u));
        index /= 10u;
    } while (index != 0 && digit_count < ARRAY_SIZE(digits));

    while (digit_count > 0 && pos + 1 < out_len) {
        out[pos++] = digits[--digit_count];
    }
    out[pos] = '\0';
}

static void usb_host_make_hub_name(char* out, size_t out_len, const char* host_name)
{
    static const char suffix[] = "-hub0";
    size_t pos = 0;

    if (!out || out_len == 0) {
        return;
    }
    if (!host_name) {
        out[0] = '\0';
        return;
    }

    while (host_name[pos] != '\0' && pos + 1 < out_len) {
        out[pos] = host_name[pos];
        pos++;
    }
    for (size_t i = 0; suffix[i] != '\0' && pos + 1 < out_len; i++) {
        out[pos++] = suffix[i];
    }
    out[pos] = '\0';
}

static void usb_host_make_port_device_name(char* out, size_t out_len, const char* host_name, uint32_t port)
{
    size_t pos = 0;
    char digits[3];
    uint32_t digit_count = 0;
    static const char prefix[] = "usb-";

    if (!out || out_len == 0) {
        return;
    }
    if (!host_name) {
        out[0] = '\0';
        return;
    }

    for (size_t i = 0; prefix[i] != '\0' && pos + 1 < out_len; i++) {
        out[pos++] = prefix[i];
    }
    for (size_t i = 0; host_name[i] != '\0' && pos + 1 < out_len; i++) {
        out[pos++] = host_name[i];
    }
    if (pos + 1 < out_len) {
        out[pos++] = '-';
    }
    if (pos + 1 < out_len) {
        out[pos++] = 'p';
    }

    do {
        digits[digit_count++] = (char)('0' + (port % 10u));
        port /= 10u;
    } while (port != 0u && digit_count < ARRAY_SIZE(digits));

    while (digit_count > 0 && pos + 1 < out_len) {
        out[pos++] = digits[--digit_count];
    }
    out[pos] = '\0';
}

static void usb_host_prepare_ep0_context(usb_host_slot_t* slot, uint32_t port)
{
    usb_port_device_info_t* info;
    uint8_t speed;
    uint8_t mps0;

    if (!slot || port == 0u || port > USB_PORT_DEVICE_MAX) {
        return;
    }

    info = &slot->port_infos[port - 1u];
    info->host_name = slot->name;
    info->port_number = (uint8_t)port;
    speed = usb_xhci_port_speed(slot, port);
    info->speed = speed;
    info->state = USB_DEVICE_STATE_ENUM_PENDING;
    info->slot_id = 0u;
    info->address = (uint8_t)port;
    mps0 = 8u;
    if (speed >= 3u) {
        mps0 = 64u;
    }
    info->max_packet_size0 = mps0;
    info->flags = 0u;
    usb_setup_get_descriptor(&info->setup,
                             USB_DESC_DEVICE,
                             0u,
                             0u,
                             8u);
}

static void usb_host_publish_port_device(usb_host_slot_t* slot, uint32_t port)
{
    fabric_device_t* dev;
    usb_device_descriptor_t desc;
    usb_device_descriptor_t desc8;
    int drc = RDNX_E_UNSUPPORTED;

    if (!slot || port == 0u || port > USB_PORT_DEVICE_MAX) {
        return;
    }
    if (slot->port_published[port - 1u]) {
        return;
    }

    usb_host_make_port_device_name(slot->port_names[port - 1u],
                                   sizeof(slot->port_names[port - 1u]),
                                   slot->name,
                                   port);
    usb_host_prepare_ep0_context(slot, port);

    if (slot->type == USB_HOST_XHCI &&
        usb_xhci_enable_slot(slot, &slot->port_infos[port - 1u]) != RDNX_OK) {
        slot->port_infos[port - 1u].state = USB_DEVICE_STATE_FAILED;
    } else if (slot->type == USB_HOST_XHCI &&
               usb_xhci_address_device(slot, &slot->port_infos[port - 1u]) != RDNX_OK) {
        fabric_log("[USB] address-device pending: %s port%u state=%s\n",
                   slot->name,
                   port,
                   usb_device_state_name(slot->port_infos[port - 1u].state));
    }

    dev = &slot->port_devices[port - 1u];
    memset(dev, 0, sizeof(*dev));
    dev->name = slot->port_names[port - 1u];
    dev->vendor_id = 0u;
    dev->device_id = 0u;
    dev->class_code = 0u;
    dev->subclass = 0u;
    dev->prog_if = 0u;
    dev->bus_private = &slot->port_infos[port - 1u];
    dev->driver_state = NULL;

    if (slot->type == USB_HOST_XHCI) {
        memset(&desc, 0, sizeof(desc));
        memset(&desc8, 0, sizeof(desc8));
        drc = usb_xhci_get_device_descriptor(slot, &slot->port_infos[port - 1u], &desc8);
        if (drc == RDNX_OK) {
            if (desc8.bMaxPacketSize0 != 0u) {
                slot->port_infos[port - 1u].max_packet_size0 = desc8.bMaxPacketSize0;
            }
            usb_setup_get_descriptor(&slot->port_infos[port - 1u].setup,
                                     USB_DESC_DEVICE,
                                     0u,
                                     0u,
                                     sizeof(desc));
            drc = usb_xhci_get_device_descriptor(slot, &slot->port_infos[port - 1u], &desc);
            if (drc == RDNX_OK) {
                dev->vendor_id = desc.idVendor;
                dev->device_id = desc.idProduct;
                dev->class_code = desc.bDeviceClass;
                dev->subclass = desc.bDeviceSubClass;
                dev->prog_if = desc.bDeviceProtocol;
            }
        }
    }

    if (fabric_device_publish(dev) == RDNX_OK) {
        slot->port_published[port - 1u] = 1u;
        fabric_log("[USB] published device %s on %s port%u\n",
                   slot->port_names[port - 1u],
                   slot->name,
                   port);
        if (drc != RDNX_OK) {
            fabric_log("[USB] device descriptor pending on %s port%u\n",
                       slot->name,
                       port);
        }
        fabric_log("[USB] ep0 prepared: req=%s addr=%u mps=%u len=%u\n",
                   usb_request_name(slot->port_infos[port - 1u].setup.bRequest),
                   slot->port_infos[port - 1u].address,
                   slot->port_infos[port - 1u].max_packet_size0,
                   slot->port_infos[port - 1u].setup.wLength);
    }
}

static int usb_host_generic_rescan(usb_host_controller_t* host)
{
    usb_host_slot_t* slot;

    if (!host || !host->context) {
        return RDNX_E_INVALID;
    }
    slot = (usb_host_slot_t*)host->context;
    if (slot->type == USB_HOST_XHCI && slot->xhci.initialized) {
        uint32_t new_bitmap = 0;

        for (uint32_t port = 0; port < slot->xhci.port_count && port < 32u; port++) {
            if (usb_xhci_port_connected(slot, port + 1u)) {
                new_bitmap |= (1u << port);
            }
        }

        uint32_t changed = slot->xhci.port_bitmap ^ new_bitmap;
        if (changed != 0u) {
            for (uint32_t port = 0; port < slot->xhci.port_count && port < 32u; port++) {
                uint32_t mask = (1u << port);
                if ((changed & mask) == 0u) {
                    continue;
                }
                fabric_log("[USB] %s port%u %s\n",
                           slot->name,
                           port + 1u,
                           (new_bitmap & mask) ? "connected" : "disconnected");
                if ((new_bitmap & mask) != 0u) {
                    usb_host_publish_port_device(slot, port + 1u);
                }
            }
            slot->xhci.port_bitmap = new_bitmap;
        }
    }
    return RDNX_OK;
}

static int usb_host_generic_poll(usb_host_controller_t* host)
{
    (void)host;
    return RDNX_OK;
}

static void usb_host_reset_slot(usb_host_slot_t* slot)
{
    if (!slot) {
        return;
    }
    memset(&slot->xhci, 0, sizeof(slot->xhci));
    memset(slot->port_devices, 0, sizeof(slot->port_devices));
    memset(slot->port_names, 0, sizeof(slot->port_names));
    memset(slot->port_infos, 0, sizeof(slot->port_infos));
    memset(slot->port_published, 0, sizeof(slot->port_published));
    slot->used = 0;
    slot->slot_index = 0u;
    slot->dev = NULL;
    slot->type = USB_HOST_UNKNOWN;
    slot->name[0] = '\0';
    slot->hub_name[0] = '\0';
}

static int usb_host_pci_attach(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < USB_HOST_SLOT_MAX; i++) {
        if (g_slots[i].used) {
            continue;
        }

        memset(&g_slots[i], 0, sizeof(g_slots[i]));
        g_slots[i].slot_index = i;
        g_slots[i].used = 1;
        g_slots[i].dev = dev;
        g_slots[i].type = usb_host_type_from_prog_if(dev->prog_if);
        usb_host_make_name(g_slots[i].name,
                           sizeof(g_slots[i].name),
                           g_slots[i].type,
                           g_usb_host_count++);
        usb_host_make_hub_name(g_slots[i].hub_name,
                               sizeof(g_slots[i].hub_name),
                               g_slots[i].name);
        g_slots[i].ops.hdr = RDNX_ABI_INIT(usb_host_ops_t);
        g_slots[i].ops.rescan = usb_host_generic_rescan;
        g_slots[i].ops.poll = usb_host_generic_poll;
        g_slots[i].host.hdr = RDNX_ABI_INIT(usb_host_controller_t);
        g_slots[i].host.name = g_slots[i].name;
        g_slots[i].host.type = g_slots[i].type;
        g_slots[i].host.vendor_id = dev->vendor_id;
        g_slots[i].host.device_id = dev->device_id;
        g_slots[i].host.prog_if = dev->prog_if;
        g_slots[i].host.root_port_count = 0;
        g_slots[i].host.flags = 0;
        g_slots[i].host.ops = &g_slots[i].ops;
        g_slots[i].host.provider_dev = dev;
        g_slots[i].host.context = &g_slots[i];

        if (g_slots[i].type == USB_HOST_XHCI) {
            int xrc = usb_xhci_init(&g_slots[i]);
            if (xrc != RDNX_OK) {
                usb_host_reset_slot(&g_slots[i]);
                return xrc;
            }
            g_slots[i].host.root_port_count = g_slots[i].xhci.port_count;
        }

        int urc = usb_host_register(&g_slots[i].host);
        if (urc != RDNX_OK) {
            usb_host_reset_slot(&g_slots[i]);
            return urc;
        }

        fabric_log("[USB] attached %s controller vendor=%x device=%x prog_if=%02x\n",
                   g_slots[i].name,
                   dev->vendor_id,
                   dev->device_id,
                   dev->prog_if);
        return RDNX_OK;
    }

    return RDNX_E_BUSY;
}

static int usb_host_pci_publish(fabric_device_t* dev)
{
    if (!dev) {
        return RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < USB_HOST_SLOT_MAX; i++) {
        if (!g_slots[i].used || g_slots[i].dev != dev) {
            continue;
        }
        if (fabric_publish_service_node(g_slots[i].name, "usb", dev) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
        if (fabric_publish_service_node(g_slots[i].hub_name, "usb-hub", dev) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
        (void)usb_host_rescan(&g_slots[i].host);
        return RDNX_OK;
    }

    return RDNX_E_NOTFOUND;
}

static void usb_host_pci_detach(fabric_device_t* dev)
{
    if (!dev) {
        return;
    }

    for (uint32_t i = 0; i < USB_HOST_SLOT_MAX; i++) {
        if (g_slots[i].used && g_slots[i].dev == dev) {
            (void)usb_host_unregister(&g_slots[i].host);
            usb_host_reset_slot(&g_slots[i]);
            return;
        }
    }
}

static fabric_driver_t g_driver = {
    .name = "usb-host-pci",
    .match_properties = usb_host_pci_match_properties,
    .match_property_count = 3,
    .match_score = usb_host_pci_match_score,
    .probe = usb_host_pci_probe,
    .attach = usb_host_pci_attach,
    .publish = usb_host_pci_publish,
    .detach = usb_host_pci_detach,
    .suspend = NULL,
    .resume = NULL
};

void usb_host_pci_init(void)
{
    int rc = fabric_driver_register(&g_driver);
    if (rc == RDNX_OK) {
        kputs("[USB] host controller driver registered\n");
    } else {
        kputs("[USB] host controller driver register failed\n");
    }
}
