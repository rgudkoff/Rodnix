/**
 * @file usb.h
 * @brief USB core helpers
 */

#ifndef _RODNIX_USB_USB_H
#define _RODNIX_USB_USB_H

#include "../../include/abi.h"
#include "usb_proto.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct fabric_device fabric_device_t;

/* Opaque handle for a USB endpoint — implementation-defined by the host driver */
typedef void* usb_ep_handle_t;

typedef enum usb_host_type {
    USB_HOST_UNKNOWN = 0,
    USB_HOST_UHCI = 1,
    USB_HOST_OHCI = 2,
    USB_HOST_EHCI = 3,
    USB_HOST_XHCI = 4
} usb_host_type_t;

#define USB_CLASS_SERIAL_BUS 0x0Cu
#define USB_SUBCLASS_USB     0x03u

#define USB_PROGIF_UHCI      0x00u
#define USB_PROGIF_OHCI      0x10u
#define USB_PROGIF_EHCI      0x20u
#define USB_PROGIF_XHCI      0x30u

typedef struct usb_host_controller usb_host_controller_t;

typedef struct usb_port_device_info {
    const char* host_name;
    usb_host_controller_t* host;  /* backlink — set by host driver on enumerate */
    uint8_t port_number;
    uint8_t speed;
    uint8_t state;
    uint8_t slot_id;
    uint8_t address;
    uint8_t max_packet_size0;
    uint16_t flags;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t config_value;
    usb_setup_packet_t setup;
} usb_port_device_info_t;

enum {
    USB_DEVICE_STATE_CONNECTED = 1u,
    USB_DEVICE_STATE_ENUM_PENDING = 2u,
    USB_DEVICE_STATE_SLOT_ENABLED = 3u,
    USB_DEVICE_STATE_ADDRESSED = 4u,
    USB_DEVICE_STATE_CONFIGURED = 5u,
    USB_DEVICE_STATE_FAILED = 6u
};

typedef struct usb_host_controller usb_host_controller_t;

typedef struct usb_host_ops {
    rdnx_abi_header_t hdr;
    int (*rescan)(usb_host_controller_t* host);
    int (*poll)(usb_host_controller_t* host);
    /* Transfer abstraction — class drivers use these instead of xHCI calls */
    usb_ep_handle_t (*find_endpoint)(usb_host_controller_t* host,
                                     usb_port_device_info_t* info,
                                     uint8_t ep_type, bool dir_in);
    int (*transfer_in)(usb_host_controller_t* host, usb_port_device_info_t* info,
                       usb_ep_handle_t ep, void* buf, uint16_t len, uint16_t* actual);
    int (*transfer_out)(usb_host_controller_t* host, usb_port_device_info_t* info,
                        usb_ep_handle_t ep, const void* buf, uint16_t len);
    int (*poll_interrupt_in)(usb_host_controller_t* host, usb_port_device_info_t* info,
                             usb_ep_handle_t ep, void* buf, uint16_t len, uint16_t* actual);
} usb_host_ops_t;

typedef struct usb_host_controller {
    rdnx_abi_header_t hdr;
    const char* name;
    uint32_t type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t prog_if;
    uint8_t root_port_count;
    uint16_t flags;
    const usb_host_ops_t* ops;
    fabric_device_t* provider_dev;
    void* context;
} usb_host_controller_t;

/* USB class driver poll registry — class drivers register their poll fn here */
typedef void (*usb_class_poll_fn_t)(void);
void usb_class_poll_register(usb_class_poll_fn_t fn);
void usb_class_poll_all(void);

void usb_init(void);
usb_host_type_t usb_host_type_from_prog_if(uint8_t prog_if);
const char* usb_host_type_name(usb_host_type_t type);
const char* usb_host_type_prefix(usb_host_type_t type);
int usb_host_register(usb_host_controller_t* host);
int usb_host_unregister(usb_host_controller_t* host);
uint32_t usb_host_count(void);
usb_host_controller_t* usb_host_get(uint32_t index);
int usb_host_rescan(usb_host_controller_t* host);
void usb_host_poll_all(void);
const char* usb_speed_name(uint8_t speed);
const char* usb_device_state_name(uint8_t state);
const char* usb_request_name(uint8_t request);
void usb_setup_packet_init(usb_setup_packet_t* pkt,
                           uint8_t request_type,
                           uint8_t request,
                           uint16_t value,
                           uint16_t index,
                           uint16_t length);
void usb_setup_get_descriptor(usb_setup_packet_t* pkt,
                              uint8_t desc_type,
                              uint8_t desc_index,
                              uint16_t lang_id,
                              uint16_t length);
void usb_setup_set_address(usb_setup_packet_t* pkt, uint8_t address);
void usb_setup_set_configuration(usb_setup_packet_t* pkt, uint8_t config_value);

#endif /* _RODNIX_USB_USB_H */
