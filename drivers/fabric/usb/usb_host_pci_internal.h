#ifndef _RODNIX_DRIVERS_FABRIC_USB_HOST_PCI_INTERNAL_H
#define _RODNIX_DRIVERS_FABRIC_USB_HOST_PCI_INTERNAL_H

#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/usb/usb.h"
#include <stdint.h>

#define USB_HOST_SLOT_MAX 8u
#define USB_HOST_NAME_MAX 16u
#define USB_HUB_NAME_MAX 24u
#define USB_PORT_DEVICE_MAX 16u
#define USB_XHCI_PENDING_EVENTS_MAX 32u

typedef struct __attribute__((packed, aligned(16))) xhci_trb {
    uint32_t dword0;
    uint32_t dword1;
    uint32_t dword2;
    uint32_t dword3;
} xhci_trb_t;

typedef struct __attribute__((packed)) xhci_erst_entry {
    uint32_t ring_base_lo;
    uint32_t ring_base_hi;
    uint32_t ring_size;
    uint32_t reserved;
} xhci_erst_entry_t;

typedef struct usb_xhci_state {
    uint64_t mmio_phys;
    volatile uint8_t* mmio_base;
    uint64_t dcbaa_phys;
    uint64_t cmd_ring_phys;
    uint64_t event_ring_phys;
    uint64_t erst_phys;
    uint64_t cmd_ring_cycle;
    uint64_t event_ring_cycle;
    uint64_t event_ring_dequeue;
    uint64_t cmd_enqueue_idx;
    uint64_t event_dequeue_idx;
    uint32_t op_base;
    uint32_t dboff;
    uint32_t rtsoff;
    uint32_t max_slots;
    uint32_t port_bitmap;
    uint8_t caplength;
    uint8_t port_count;
    uint8_t initialized;
    uint8_t running;
    uint8_t pending_head;
    uint8_t pending_tail;
    uint8_t pending_count;
    xhci_trb_t* cmd_ring;
    xhci_trb_t* event_ring;
    xhci_erst_entry_t* erst;
    uint64_t* dcbaa;
    xhci_trb_t pending_events[USB_XHCI_PENDING_EVENTS_MAX];
} usb_xhci_state_t;

typedef struct usb_xhci_port_ctx {
    uint64_t input_ctx_phys;
    uint64_t output_ctx_phys;
    uint64_t ep0_ring_phys;
    uint64_t transfer_buf_phys;
    void* input_ctx;
    void* output_ctx;
    xhci_trb_t* ep0_ring;
    void* transfer_buf;
    uint8_t slot_id;
    uint8_t context_ready;
    uint8_t ep0_cycle;
    uint16_t reserved0;
} usb_xhci_port_ctx_t;

typedef struct usb_host_slot {
    int used;
    uint32_t slot_index;
    fabric_device_t* dev;
    usb_host_type_t type;
    char name[USB_HOST_NAME_MAX];
    char hub_name[USB_HUB_NAME_MAX];
    usb_host_ops_t ops;
    usb_host_controller_t host;
    usb_xhci_state_t xhci;
    usb_xhci_port_ctx_t xhci_ports[USB_PORT_DEVICE_MAX];
    fabric_device_t port_devices[USB_PORT_DEVICE_MAX];
    char port_names[USB_PORT_DEVICE_MAX][16];
    usb_port_device_info_t port_infos[USB_PORT_DEVICE_MAX];
    uint8_t port_published[USB_PORT_DEVICE_MAX];
} usb_host_slot_t;

int usb_xhci_init(usb_host_slot_t* slot);
int usb_xhci_enable_slot(usb_host_slot_t* slot, usb_port_device_info_t* info);
int usb_xhci_address_device(usb_host_slot_t* slot, usb_port_device_info_t* info);
int usb_xhci_get_device_descriptor(usb_host_slot_t* slot,
                                   usb_port_device_info_t* info,
                                   usb_device_descriptor_t* out_desc);
uint32_t usb_xhci_portsc(const usb_host_slot_t* slot, uint32_t port);
uint8_t usb_xhci_port_speed(const usb_host_slot_t* slot, uint32_t port);
int usb_xhci_port_connected(const usb_host_slot_t* slot, uint32_t port);

#endif
