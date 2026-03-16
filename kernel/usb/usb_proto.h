/**
 * @file usb_proto.h
 * @brief USB protocol definitions
 */

#ifndef _RODNIX_USB_USB_PROTO_H
#define _RODNIX_USB_USB_PROTO_H

#include <stdint.h>

enum {
    USB_DIR_OUT = 0x00u,
    USB_DIR_IN  = 0x80u
};

enum {
    USB_TYPE_STANDARD = 0x00u,
    USB_TYPE_CLASS    = 0x20u,
    USB_TYPE_VENDOR   = 0x40u
};

enum {
    USB_RECIP_DEVICE    = 0x00u,
    USB_RECIP_INTERFACE = 0x01u,
    USB_RECIP_ENDPOINT  = 0x02u
};

enum {
    USB_REQ_GET_STATUS        = 0x00u,
    USB_REQ_CLEAR_FEATURE     = 0x01u,
    USB_REQ_SET_FEATURE       = 0x03u,
    USB_REQ_SET_ADDRESS       = 0x05u,
    USB_REQ_GET_DESCRIPTOR    = 0x06u,
    USB_REQ_SET_DESCRIPTOR    = 0x07u,
    USB_REQ_GET_CONFIGURATION = 0x08u,
    USB_REQ_SET_CONFIGURATION = 0x09u
};

enum {
    USB_DESC_DEVICE        = 0x01u,
    USB_DESC_CONFIGURATION = 0x02u,
    USB_DESC_STRING        = 0x03u,
    USB_DESC_INTERFACE     = 0x04u,
    USB_DESC_ENDPOINT      = 0x05u,
    USB_DESC_HID           = 0x21u,
    USB_DESC_REPORT        = 0x22u
};

typedef struct __attribute__((packed)) usb_setup_packet {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

typedef struct __attribute__((packed)) usb_device_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} usb_device_descriptor_t;

typedef struct __attribute__((packed)) usb_config_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} usb_config_descriptor_t;

#endif /* _RODNIX_USB_USB_PROTO_H */
