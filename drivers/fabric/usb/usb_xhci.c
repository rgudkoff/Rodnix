/**
 * @file usb_xhci.c
 * @brief Internal xHCI backend for the PCI USB host driver
 */

#include "usb_host_pci_internal.h"
#include "../../../kernel/fabric/bus/pci.h"
#include "../../../kernel/core/memory.h"
#include "../../../kernel/arch/x86_64/paging.h"
#include "../../../kernel/arch/x86_64/config.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include <stdbool.h>
#include <stdint.h>

#define XHCI_MMIO_SIZE      0x10000u
#define XHCI_MMIO_VIRT_BASE 0xFFFFFFFFFED00000ULL

#define XHCI_CAPLENGTH      0x00u
#define XHCI_HCSPARAMS1     0x04u
#define XHCI_DBOFF          0x14u
#define XHCI_RTSOFF         0x18u

#define XHCI_USBCMD         0x00u
#define XHCI_USBSTS         0x04u
#define XHCI_PAGESIZE       0x08u
#define XHCI_CRCR_LO        0x18u
#define XHCI_DCBAAP_LO      0x30u
#define XHCI_CONFIG         0x38u
#define XHCI_PORTREG_BASE   0x400u
#define XHCI_PORTREG_SIZE   0x10u
#define XHCI_INTR0_BASE     0x20u
#define XHCI_IMAN           0x00u
#define XHCI_IMOD           0x04u
#define XHCI_ERSTSZ         0x08u
#define XHCI_ERSTBA_LO      0x10u
#define XHCI_ERDP_LO        0x18u

#define XHCI_USBCMD_RUN     (1u << 0)
#define XHCI_USBCMD_HCRST   (1u << 1)
#define XHCI_USBSTS_HCH     (1u << 0)
#define XHCI_USBSTS_CNR     (1u << 11)
#define XHCI_IMAN_IE        (1u << 1)
#define XHCI_PORTSC_CCS     (1u << 0)
#define XHCI_PORTSC_SPEED_SHIFT 10u
#define XHCI_PORTSC_SPEED_MASK  (0xFu << XHCI_PORTSC_SPEED_SHIFT)
#define XHCI_CRCR_RCS       (1u << 0)
#define XHCI_TRB_CYCLE      (1u << 0)
#define XHCI_TRB_TYPE_SHIFT 10u
#define XHCI_TRB_TYPE_MASK  (0x3Fu << XHCI_TRB_TYPE_SHIFT)

#define XHCI_TRB_ENABLE_SLOT_CMD   9u
#define XHCI_TRB_ADDRESS_DEVICE_CMD 11u
#define XHCI_TRB_SETUP_STAGE       2u
#define XHCI_TRB_DATA_STAGE        3u
#define XHCI_TRB_STATUS_STAGE      4u
#define XHCI_TRB_NOOP_CMD          23u
#define XHCI_TRB_TRANSFER_EVENT    32u
#define XHCI_TRB_CMD_COMPLETION_EV 33u

#define XHCI_CC_SUCCESS 1u

#define XHCI_CMD_RING_TRBS   256u
#define XHCI_EVENT_RING_TRBS 256u
#define XHCI_EP0_RING_TRBS   16u

#define XHCI_CTX_STRIDE              0x20u
#define XHCI_INPUT_CONTROL_CTX_SIZE  0x20u
#define XHCI_SLOT_CTX_OFFSET         XHCI_INPUT_CONTROL_CTX_SIZE
#define XHCI_EP0_CTX_OFFSET          (XHCI_INPUT_CONTROL_CTX_SIZE + XHCI_CTX_STRIDE)

#define XHCI_SLOT_CTX_DWORD0_SPEED_SHIFT       20u
#define XHCI_SLOT_CTX_DWORD0_CTX_ENTRIES_SHIFT 27u
#define XHCI_SLOT_CTX_DWORD1_ROOT_PORT_SHIFT   16u

#define XHCI_EP_CTX_DWORD1_EP_TYPE_SHIFT       3u
#define XHCI_EP_CTX_DWORD1_CERR_SHIFT          1u
#define XHCI_EP_CTX_DWORD1_MAX_PACKET_SHIFT    16u

#define XHCI_EP_TYPE_CONTROL 4u
#define XHCI_TRB_LINK        6u

#define XHCI_TRB_ENT         (1u << 1)
#define XHCI_TRB_CH          (1u << 4)
#define XHCI_TRB_IOC         (1u << 5)
#define XHCI_TRB_IDT         (1u << 6)
#define XHCI_TRB_DIR_IN      (1u << 16)
#define XHCI_TRB_TRT_SHIFT   16u

#define XHCI_SETUP_TRT_NONE  0u
#define XHCI_SETUP_TRT_OUT   2u
#define XHCI_SETUP_TRT_IN    3u

static inline uint32_t usb_xhci_rd32(const usb_xhci_state_t* xhci, uint32_t reg)
{
    return *(volatile uint32_t*)(xhci->mmio_base + reg);
}

static inline void usb_xhci_wr32(const usb_xhci_state_t* xhci, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t*)(xhci->mmio_base + reg) = value;
    __asm__ volatile ("" ::: "memory");
}

static inline uint64_t usb_xhci_rd64(const usb_xhci_state_t* xhci, uint32_t reg_lo)
{
    uint64_t lo = usb_xhci_rd32(xhci, reg_lo);
    uint64_t hi = usb_xhci_rd32(xhci, reg_lo + 4u);
    return lo | (hi << 32);
}

static inline void usb_xhci_wr64(const usb_xhci_state_t* xhci, uint32_t reg_lo, uint64_t value)
{
    usb_xhci_wr32(xhci, reg_lo, (uint32_t)(value & 0xFFFFFFFFu));
    usb_xhci_wr32(xhci, reg_lo + 4u, (uint32_t)(value >> 32));
}

static void* usb_xhci_alloc_dma(size_t size, uint64_t* phys_out)
{
    uint32_t pages = (uint32_t)(ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE);
    uint64_t phys;
    void* virt;

    if (pages == 0u) {
        pages = 1u;
    }

    phys = pmm_alloc_pages(pages);
    if (!phys) {
        return NULL;
    }

    virt = X86_64_PHYS_TO_VIRT(phys);
    if (!virt) {
        pmm_free_pages(phys, pages);
        return NULL;
    }

    memset(virt, 0, (size_t)pages * PAGE_SIZE);
    if (phys_out) {
        *phys_out = phys;
    }
    return virt;
}

static int usb_xhci_setup_rings(usb_host_slot_t* slot)
{
    usb_xhci_state_t* xhci;
    uint32_t intr_base;

    if (!slot) {
        return RDNX_E_INVALID;
    }
    xhci = &slot->xhci;

    xhci->dcbaa = (uint64_t*)usb_xhci_alloc_dma((size_t)(xhci->max_slots + 1u) * sizeof(uint64_t),
                                                &xhci->dcbaa_phys);
    xhci->cmd_ring = (xhci_trb_t*)usb_xhci_alloc_dma(sizeof(xhci_trb_t) * XHCI_CMD_RING_TRBS,
                                                     &xhci->cmd_ring_phys);
    xhci->event_ring = (xhci_trb_t*)usb_xhci_alloc_dma(sizeof(xhci_trb_t) * XHCI_EVENT_RING_TRBS,
                                                       &xhci->event_ring_phys);
    xhci->erst = (xhci_erst_entry_t*)usb_xhci_alloc_dma(sizeof(xhci_erst_entry_t),
                                                        &xhci->erst_phys);
    if (!xhci->dcbaa || !xhci->cmd_ring || !xhci->event_ring || !xhci->erst) {
        return RDNX_E_NOMEM;
    }

    xhci->cmd_ring_cycle = 1u;
    xhci->event_ring_cycle = 1u;
    xhci->event_ring_dequeue = xhci->event_ring_phys;
    xhci->cmd_enqueue_idx = 0u;
    xhci->event_dequeue_idx = 0u;

    xhci->cmd_ring[XHCI_CMD_RING_TRBS - 1u].dword0 = (uint32_t)(xhci->cmd_ring_phys & 0xFFFFFFFFu);
    xhci->cmd_ring[XHCI_CMD_RING_TRBS - 1u].dword1 = (uint32_t)(xhci->cmd_ring_phys >> 32);
    xhci->cmd_ring[XHCI_CMD_RING_TRBS - 1u].dword3 = (6u << 10) | 1u;

    xhci->erst[0].ring_base_lo = (uint32_t)(xhci->event_ring_phys & 0xFFFFFFFFu);
    xhci->erst[0].ring_base_hi = (uint32_t)(xhci->event_ring_phys >> 32);
    xhci->erst[0].ring_size = XHCI_EVENT_RING_TRBS;

    usb_xhci_wr64(xhci, xhci->op_base + XHCI_DCBAAP_LO, xhci->dcbaa_phys);
    usb_xhci_wr64(xhci,
                  xhci->op_base + XHCI_CRCR_LO,
                  (xhci->cmd_ring_phys & ~0x3Fu) | XHCI_CRCR_RCS);

    intr_base = xhci->rtsoff + XHCI_INTR0_BASE;
    usb_xhci_wr32(xhci, intr_base + XHCI_IMOD, 0u);
    usb_xhci_wr32(xhci, intr_base + XHCI_ERSTSZ, 1u);
    usb_xhci_wr64(xhci, intr_base + XHCI_ERSTBA_LO, xhci->erst_phys);
    usb_xhci_wr64(xhci, intr_base + XHCI_ERDP_LO, xhci->event_ring_dequeue);
    usb_xhci_wr32(xhci, intr_base + XHCI_IMAN, XHCI_IMAN_IE);

    fabric_log("[USB] xHCI rings ready: dcbaa=%llx cmd=%llx evt=%llx erst=%llx\n",
               (unsigned long long)xhci->dcbaa_phys,
               (unsigned long long)xhci->cmd_ring_phys,
               (unsigned long long)xhci->event_ring_phys,
               (unsigned long long)xhci->erst_phys);
    return RDNX_OK;
}

static uint32_t usb_xhci_trb_type(const xhci_trb_t* trb)
{
    if (!trb) {
        return 0u;
    }
    return (trb->dword3 & XHCI_TRB_TYPE_MASK) >> XHCI_TRB_TYPE_SHIFT;
}

static uint32_t usb_xhci_completion_code(const xhci_trb_t* trb)
{
    if (!trb) {
        return 0u;
    }
    return (trb->dword2 >> 24) & 0xFFu;
}

static uint32_t usb_xhci_transfer_remaining(const xhci_trb_t* trb)
{
    if (!trb) {
        return 0u;
    }
    return trb->dword2 & 0x00FFFFFFu;
}

static uint32_t usb_xhci_completion_slot_id(const xhci_trb_t* trb)
{
    if (!trb) {
        return 0u;
    }
    return (trb->dword3 >> 24) & 0xFFu;
}

static uint32_t* usb_xhci_input_control_ctx(const usb_xhci_port_ctx_t* port_ctx)
{
    return port_ctx ? (uint32_t*)port_ctx->input_ctx : NULL;
}

static uint32_t* usb_xhci_input_slot_ctx(const usb_xhci_port_ctx_t* port_ctx)
{
    return port_ctx ? (uint32_t*)((uint8_t*)port_ctx->input_ctx + XHCI_SLOT_CTX_OFFSET) : NULL;
}

static uint32_t* usb_xhci_input_ep0_ctx(const usb_xhci_port_ctx_t* port_ctx)
{
    return port_ctx ? (uint32_t*)((uint8_t*)port_ctx->input_ctx + XHCI_EP0_CTX_OFFSET) : NULL;
}

static int usb_xhci_ring_command(usb_host_slot_t* slot, const xhci_trb_t* trb)
{
    usb_xhci_state_t* xhci;
    uint32_t idx;
    xhci_trb_t cmd;

    if (!slot || !trb) {
        return RDNX_E_INVALID;
    }
    xhci = &slot->xhci;
    if (!xhci->cmd_ring || !xhci->running) {
        return RDNX_E_INVALID;
    }

    idx = (uint32_t)(xhci->cmd_enqueue_idx % (XHCI_CMD_RING_TRBS - 1u));
    cmd = *trb;
    if (xhci->cmd_ring_cycle != 0u) {
        cmd.dword3 |= XHCI_TRB_CYCLE;
    } else {
        cmd.dword3 &= ~XHCI_TRB_CYCLE;
    }
    xhci->cmd_ring[idx] = cmd;
    __asm__ volatile ("" ::: "memory");

    xhci->cmd_enqueue_idx++;
    usb_xhci_wr32(xhci, xhci->dboff + 0u, 0u);
    return RDNX_OK;
}

static int usb_xhci_pending_push(usb_host_slot_t* slot, const xhci_trb_t* ev)
{
    usb_xhci_state_t* xhci;
    uint32_t idx;

    if (!slot || !ev) {
        return RDNX_E_INVALID;
    }
    xhci = &slot->xhci;
    if (xhci->pending_count >= USB_XHCI_PENDING_EVENTS_MAX) {
        return RDNX_E_BUSY;
    }

    idx = xhci->pending_tail % USB_XHCI_PENDING_EVENTS_MAX;
    xhci->pending_events[idx] = *ev;
    xhci->pending_tail = (uint8_t)((xhci->pending_tail + 1u) % USB_XHCI_PENDING_EVENTS_MAX);
    xhci->pending_count++;
    return RDNX_OK;
}

static int usb_xhci_pending_pop(usb_host_slot_t* slot, xhci_trb_t* out)
{
    usb_xhci_state_t* xhci;
    uint32_t idx;

    if (!slot || !out) {
        return RDNX_E_INVALID;
    }
    xhci = &slot->xhci;
    if (xhci->pending_count == 0u) {
        return RDNX_E_NOTFOUND;
    }

    idx = xhci->pending_head % USB_XHCI_PENDING_EVENTS_MAX;
    *out = xhci->pending_events[idx];
    memset(&xhci->pending_events[idx], 0, sizeof(xhci->pending_events[idx]));
    xhci->pending_head = (uint8_t)((xhci->pending_head + 1u) % USB_XHCI_PENDING_EVENTS_MAX);
    xhci->pending_count--;
    return RDNX_OK;
}

static int usb_xhci_poll_event(usb_host_slot_t* slot, xhci_trb_t* out)
{
    usb_xhci_state_t* xhci;
    uint32_t idx;
    xhci_trb_t* trb;

    if (!slot) {
        return RDNX_E_INVALID;
    }
    if (out && usb_xhci_pending_pop(slot, out) == RDNX_OK) {
        return RDNX_OK;
    }
    xhci = &slot->xhci;
    if (!xhci->event_ring) {
        return RDNX_E_INVALID;
    }

    idx = (uint32_t)(xhci->event_dequeue_idx % XHCI_EVENT_RING_TRBS);
    trb = &xhci->event_ring[idx];
    if (((trb->dword3 & XHCI_TRB_CYCLE) ? 1u : 0u) != (uint32_t)xhci->event_ring_cycle) {
        return RDNX_E_BUSY;
    }

    if (out) {
        *out = *trb;
    }
    memset(trb, 0, sizeof(*trb));

    xhci->event_dequeue_idx++;
    xhci->event_ring_dequeue = xhci->event_ring_phys +
        ((xhci->event_dequeue_idx % XHCI_EVENT_RING_TRBS) * sizeof(xhci_trb_t));
    usb_xhci_wr64(xhci, xhci->rtsoff + XHCI_INTR0_BASE + XHCI_ERDP_LO, xhci->event_ring_dequeue);
    return RDNX_OK;
}

static int usb_xhci_wait_command_completion(usb_host_slot_t* slot, xhci_trb_t* out_ev)
{
    xhci_trb_t ev;

    if (!slot) {
        return RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < 1000000u; i++) {
        if (usb_xhci_poll_event(slot, &ev) == RDNX_OK) {
            if (usb_xhci_trb_type(&ev) == XHCI_TRB_CMD_COMPLETION_EV) {
                if (out_ev) {
                    *out_ev = ev;
                }
                return RDNX_OK;
            }
            (void)usb_xhci_pending_push(slot, &ev);
        }
        __asm__ volatile ("pause");
    }

    return RDNX_E_TIMEOUT;
}

static int usb_xhci_wait_transfer_event(usb_host_slot_t* slot, uint8_t slot_id, xhci_trb_t* out_ev)
{
    xhci_trb_t ev;

    if (!slot || slot_id == 0u) {
        return RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < 1000000u; i++) {
        if (usb_xhci_poll_event(slot, &ev) == RDNX_OK) {
            uint32_t type = usb_xhci_trb_type(&ev);
            if (type == XHCI_TRB_TRANSFER_EVENT &&
                usb_xhci_completion_slot_id(&ev) == slot_id) {
                if (out_ev) {
                    *out_ev = ev;
                }
                return RDNX_OK;
            }
            (void)usb_xhci_pending_push(slot, &ev);
        }
        __asm__ volatile ("pause");
    }

    return RDNX_E_TIMEOUT;
}

static int usb_xhci_noop(usb_host_slot_t* slot)
{
    xhci_trb_t cmd;
    xhci_trb_t ev;

    if (!slot) {
        return RDNX_E_INVALID;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.dword3 = (XHCI_TRB_NOOP_CMD << XHCI_TRB_TYPE_SHIFT);
    if (usb_xhci_ring_command(slot, &cmd) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    if (usb_xhci_wait_command_completion(slot, &ev) != RDNX_OK) {
        return RDNX_E_TIMEOUT;
    }

    fabric_log("[USB] xHCI noop complete: status=%08x control=%08x\n",
               ev.dword2,
               ev.dword3);
    return RDNX_OK;
}

static int usb_xhci_prepare_slot_contexts(usb_host_slot_t* slot, usb_port_device_info_t* info)
{
    usb_xhci_port_ctx_t* port_ctx;
    uint32_t port_index;

    if (!slot || !info || info->port_number == 0u || info->port_number > USB_PORT_DEVICE_MAX) {
        return RDNX_E_INVALID;
    }

    port_index = (uint32_t)(info->port_number - 1u);
    port_ctx = &slot->xhci_ports[port_index];
    if (port_ctx->context_ready) {
        return RDNX_OK;
    }

    port_ctx->input_ctx = usb_xhci_alloc_dma(PAGE_SIZE, &port_ctx->input_ctx_phys);
    port_ctx->output_ctx = usb_xhci_alloc_dma(PAGE_SIZE, &port_ctx->output_ctx_phys);
    port_ctx->ep0_ring = (xhci_trb_t*)usb_xhci_alloc_dma(sizeof(xhci_trb_t) * XHCI_EP0_RING_TRBS,
                                                         &port_ctx->ep0_ring_phys);
    port_ctx->transfer_buf = usb_xhci_alloc_dma(PAGE_SIZE, &port_ctx->transfer_buf_phys);
    if (!port_ctx->input_ctx || !port_ctx->output_ctx || !port_ctx->ep0_ring || !port_ctx->transfer_buf) {
        return RDNX_E_NOMEM;
    }

    port_ctx->ep0_cycle = 1u;
    port_ctx->ep0_ring[XHCI_EP0_RING_TRBS - 1u].dword0 = (uint32_t)(port_ctx->ep0_ring_phys & 0xFFFFFFFFu);
    port_ctx->ep0_ring[XHCI_EP0_RING_TRBS - 1u].dword1 = (uint32_t)(port_ctx->ep0_ring_phys >> 32);
    port_ctx->ep0_ring[XHCI_EP0_RING_TRBS - 1u].dword3 = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE;

    port_ctx->slot_id = info->slot_id;
    port_ctx->context_ready = 1u;
    if (info->slot_id < (slot->xhci.max_slots + 1u) && slot->xhci.dcbaa) {
        slot->xhci.dcbaa[info->slot_id] = port_ctx->output_ctx_phys;
    }

    fabric_log("[USB] slot contexts ready: %s port%u slot=%u in=%llx out=%llx\n",
               slot->name,
               info->port_number,
               info->slot_id,
               (unsigned long long)port_ctx->input_ctx_phys,
               (unsigned long long)port_ctx->output_ctx_phys);
    return RDNX_OK;
}

static void usb_xhci_build_address_input_ctx(usb_host_slot_t* slot,
                                             usb_port_device_info_t* info,
                                             usb_xhci_port_ctx_t* port_ctx)
{
    uint32_t* control;
    uint32_t* slot_ctx;
    uint32_t* ep0_ctx;
    uint32_t mps = info->max_packet_size0 ? info->max_packet_size0 : 8u;

    (void)slot;

    memset(port_ctx->input_ctx, 0, PAGE_SIZE);
    control = usb_xhci_input_control_ctx(port_ctx);
    slot_ctx = usb_xhci_input_slot_ctx(port_ctx);
    ep0_ctx = usb_xhci_input_ep0_ctx(port_ctx);
    if (!control || !slot_ctx || !ep0_ctx) {
        return;
    }

    control[1] = 0x3u;

    slot_ctx[0] = (1u << XHCI_SLOT_CTX_DWORD0_CTX_ENTRIES_SHIFT) |
                  ((uint32_t)info->speed << XHCI_SLOT_CTX_DWORD0_SPEED_SHIFT);
    slot_ctx[1] = ((uint32_t)info->port_number << XHCI_SLOT_CTX_DWORD1_ROOT_PORT_SHIFT);

    ep0_ctx[1] = (3u << XHCI_EP_CTX_DWORD1_CERR_SHIFT) |
                 (XHCI_EP_TYPE_CONTROL << XHCI_EP_CTX_DWORD1_EP_TYPE_SHIFT) |
                 (mps << XHCI_EP_CTX_DWORD1_MAX_PACKET_SHIFT);
    ep0_ctx[2] = (uint32_t)(port_ctx->ep0_ring_phys & ~0xFu) | (uint32_t)(port_ctx->ep0_cycle & 0x1u);
    ep0_ctx[3] = (uint32_t)(port_ctx->ep0_ring_phys >> 32);
    ep0_ctx[4] = 8u;
}

static void usb_xhci_prepare_ep0_transfer_ring(usb_xhci_port_ctx_t* port_ctx,
                                               const usb_setup_packet_t* setup,
                                               uint32_t transfer_len)
{
    uint32_t req0;
    uint32_t req1;

    if (!port_ctx || !port_ctx->ep0_ring || !setup || !port_ctx->transfer_buf) {
        return;
    }

    memset(port_ctx->ep0_ring, 0, sizeof(xhci_trb_t) * XHCI_EP0_RING_TRBS);
    memset(port_ctx->transfer_buf, 0, PAGE_SIZE);

    req0 = ((uint32_t)setup->bmRequestType) |
           ((uint32_t)setup->bRequest << 8) |
           ((uint32_t)setup->wValue << 16);
    req1 = ((uint32_t)setup->wIndex) |
           ((uint32_t)setup->wLength << 16);

    port_ctx->ep0_ring[0].dword0 = req0;
    port_ctx->ep0_ring[0].dword1 = req1;
    port_ctx->ep0_ring[0].dword2 = 8u;
    port_ctx->ep0_ring[0].dword3 = XHCI_TRB_CYCLE |
                                   XHCI_TRB_CH |
                                   XHCI_TRB_IDT |
                                   (XHCI_SETUP_TRT_IN << XHCI_TRB_TRT_SHIFT) |
                                   (XHCI_TRB_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT);

    port_ctx->ep0_ring[1].dword0 = (uint32_t)(port_ctx->transfer_buf_phys & 0xFFFFFFFFu);
    port_ctx->ep0_ring[1].dword1 = (uint32_t)(port_ctx->transfer_buf_phys >> 32);
    port_ctx->ep0_ring[1].dword2 = transfer_len;
    port_ctx->ep0_ring[1].dword3 = XHCI_TRB_CYCLE |
                                   XHCI_TRB_CH |
                                   XHCI_TRB_DIR_IN |
                                   (XHCI_TRB_DATA_STAGE << XHCI_TRB_TYPE_SHIFT);

    port_ctx->ep0_ring[2].dword3 = XHCI_TRB_CYCLE |
                                   XHCI_TRB_IOC |
                                   (XHCI_TRB_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT);

    port_ctx->ep0_ring[XHCI_EP0_RING_TRBS - 1u].dword0 = (uint32_t)(port_ctx->ep0_ring_phys & 0xFFFFFFFFu);
    port_ctx->ep0_ring[XHCI_EP0_RING_TRBS - 1u].dword1 = (uint32_t)(port_ctx->ep0_ring_phys >> 32);
    port_ctx->ep0_ring[XHCI_EP0_RING_TRBS - 1u].dword3 = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE;
}

static void usb_xhci_ring_endpoint_doorbell(const usb_host_slot_t* slot, uint8_t slot_id, uint8_t endpoint_id)
{
    if (!slot || slot_id == 0u) {
        return;
    }
    usb_xhci_wr32(&slot->xhci, slot->xhci.dboff + ((uint32_t)slot_id * 4u), endpoint_id);
}

int usb_xhci_enable_slot(usb_host_slot_t* slot, usb_port_device_info_t* info)
{
    xhci_trb_t cmd;
    xhci_trb_t ev;
    uint32_t completion;
    uint32_t slot_id;

    if (!slot || !info) {
        return RDNX_E_INVALID;
    }
    if (info->slot_id != 0u) {
        return RDNX_OK;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.dword3 = (XHCI_TRB_ENABLE_SLOT_CMD << XHCI_TRB_TYPE_SHIFT);
    if (usb_xhci_ring_command(slot, &cmd) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    if (usb_xhci_wait_command_completion(slot, &ev) != RDNX_OK) {
        return RDNX_E_TIMEOUT;
    }

    completion = usb_xhci_completion_code(&ev);
    slot_id = usb_xhci_completion_slot_id(&ev);
    if (completion != XHCI_CC_SUCCESS || slot_id == 0u) {
        fabric_log("[USB] enable-slot failed: cc=%u status=%08x control=%08x\n",
                   completion,
                   ev.dword2,
                   ev.dword3);
        return RDNX_E_GENERIC;
    }

    info->slot_id = (uint8_t)slot_id;
    if (usb_xhci_prepare_slot_contexts(slot, info) != RDNX_OK) {
        return RDNX_E_NOMEM;
    }
    info->state = USB_DEVICE_STATE_SLOT_ENABLED;
    fabric_log("[USB] slot enabled: %s port%u slot=%u\n",
               slot->name,
               info->port_number,
               slot_id);
    return RDNX_OK;
}

int usb_xhci_address_device(usb_host_slot_t* slot, usb_port_device_info_t* info)
{
    usb_xhci_port_ctx_t* port_ctx;
    xhci_trb_t cmd;
    xhci_trb_t ev;
    uint32_t completion;
    uint32_t port_index;

    if (!slot || !info || info->port_number == 0u || info->port_number > USB_PORT_DEVICE_MAX) {
        return RDNX_E_INVALID;
    }
    if (info->state >= USB_DEVICE_STATE_ADDRESSED) {
        return RDNX_OK;
    }

    port_index = (uint32_t)(info->port_number - 1u);
    port_ctx = &slot->xhci_ports[port_index];
    if (!port_ctx->context_ready || port_ctx->slot_id == 0u) {
        return RDNX_E_INVALID;
    }

    usb_xhci_build_address_input_ctx(slot, info, port_ctx);

    memset(&cmd, 0, sizeof(cmd));
    cmd.dword0 = (uint32_t)(port_ctx->input_ctx_phys & 0xFFFFFFFFu);
    cmd.dword1 = (uint32_t)(port_ctx->input_ctx_phys >> 32);
    cmd.dword3 = (XHCI_TRB_ADDRESS_DEVICE_CMD << XHCI_TRB_TYPE_SHIFT) |
                 ((uint32_t)port_ctx->slot_id << 24);

    if (usb_xhci_ring_command(slot, &cmd) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }
    if (usb_xhci_wait_command_completion(slot, &ev) != RDNX_OK) {
        return RDNX_E_TIMEOUT;
    }

    completion = usb_xhci_completion_code(&ev);
    if (completion != XHCI_CC_SUCCESS) {
        fabric_log("[USB] address-device failed: port%u slot=%u cc=%u status=%08x control=%08x\n",
                   info->port_number,
                   port_ctx->slot_id,
                   completion,
                   ev.dword2,
                   ev.dword3);
        return RDNX_E_GENERIC;
    }

    info->address = port_ctx->slot_id;
    info->state = USB_DEVICE_STATE_ADDRESSED;
    fabric_log("[USB] device addressed: %s port%u slot=%u addr=%u\n",
               slot->name,
               info->port_number,
               port_ctx->slot_id,
               info->address);
    return RDNX_OK;
}

int usb_xhci_get_device_descriptor(usb_host_slot_t* slot,
                                   usb_port_device_info_t* info,
                                   usb_device_descriptor_t* out_desc)
{
    usb_xhci_port_ctx_t* port_ctx;
    xhci_trb_t ev;
    uint32_t completion;
    uint32_t port_index;
    uint16_t transfer_len;

    if (!slot || !info || !out_desc || info->port_number == 0u || info->port_number > USB_PORT_DEVICE_MAX) {
        return RDNX_E_INVALID;
    }
    if (info->state < USB_DEVICE_STATE_ADDRESSED) {
        return RDNX_E_BUSY;
    }

    port_index = (uint32_t)(info->port_number - 1u);
    port_ctx = &slot->xhci_ports[port_index];
    if (!port_ctx->context_ready || port_ctx->slot_id == 0u || !port_ctx->transfer_buf) {
        return RDNX_E_INVALID;
    }

    transfer_len = info->setup.wLength;
    if (transfer_len == 0u || transfer_len > sizeof(usb_device_descriptor_t)) {
        transfer_len = sizeof(usb_device_descriptor_t);
    }

    usb_xhci_prepare_ep0_transfer_ring(port_ctx, &info->setup, transfer_len);
    usb_xhci_ring_endpoint_doorbell(slot, port_ctx->slot_id, 1u);
    if (usb_xhci_wait_transfer_event(slot, port_ctx->slot_id, &ev) != RDNX_OK) {
        fabric_log("[USB] get-device-desc timeout: port%u slot=%u\n",
                   info->port_number,
                   port_ctx->slot_id);
        return RDNX_E_TIMEOUT;
    }

    completion = usb_xhci_completion_code(&ev);
    if (completion != XHCI_CC_SUCCESS) {
        fabric_log("[USB] get-device-desc failed: port%u slot=%u cc=%u status=%08x control=%08x\n",
                   info->port_number,
                   port_ctx->slot_id,
                   completion,
                   ev.dword2,
                   ev.dword3);
        return RDNX_E_GENERIC;
    }

    memset(out_desc, 0, sizeof(*out_desc));
    memcpy(out_desc, port_ctx->transfer_buf, transfer_len);
    fabric_log("[USB] get-device-desc data: remain=%u raw=%02x %02x %02x %02x %02x %02x %02x %02x\n",
               usb_xhci_transfer_remaining(&ev),
               ((const uint8_t*)port_ctx->transfer_buf)[0],
               ((const uint8_t*)port_ctx->transfer_buf)[1],
               ((const uint8_t*)port_ctx->transfer_buf)[2],
               ((const uint8_t*)port_ctx->transfer_buf)[3],
               ((const uint8_t*)port_ctx->transfer_buf)[4],
               ((const uint8_t*)port_ctx->transfer_buf)[5],
               ((const uint8_t*)port_ctx->transfer_buf)[6],
               ((const uint8_t*)port_ctx->transfer_buf)[7]);
    fabric_log("[USB] device descriptor: port%u vid=%04x pid=%04x class=%02x proto=%02x mps=%u cfgs=%u\n",
               info->port_number,
               out_desc->idVendor,
               out_desc->idProduct,
               out_desc->bDeviceClass,
               out_desc->bDeviceProtocol,
               out_desc->bMaxPacketSize0,
               out_desc->bNumConfigurations);
    return RDNX_OK;
}

static int usb_xhci_wait_for(const usb_xhci_state_t* xhci, uint32_t reg, uint32_t mask, bool set)
{
    if (!xhci || !xhci->mmio_base) {
        return RDNX_E_INVALID;
    }

    for (uint32_t i = 0; i < 1000000u; i++) {
        uint32_t value = usb_xhci_rd32(xhci, reg);
        if (set) {
            if ((value & mask) == mask) {
                return RDNX_OK;
            }
        } else if ((value & mask) == 0u) {
            return RDNX_OK;
        }
        __asm__ volatile ("pause");
    }

    return RDNX_E_TIMEOUT;
}

static int usb_xhci_map_mmio(usb_host_slot_t* slot)
{
    pci_device_info_t* pci;
    uint64_t mmio_phys;
    uint64_t mmio_virt;
    uint64_t mmio_flags = PTE_PRESENT | PTE_RW | PTE_PCD;
    uint32_t bar0;
    uint32_t bar1;

    if (!slot || !slot->dev || !slot->dev->bus_private) {
        return RDNX_E_INVALID;
    }

    pci = (pci_device_info_t*)slot->dev->bus_private;
    bar0 = pci->bars[0];
    bar1 = pci->bars[1];
    if ((bar0 & 0x1u) != 0u) {
        return RDNX_E_UNSUPPORTED;
    }

    mmio_phys = (uint64_t)(bar0 & ~0xFu);
    if ((bar0 & 0x6u) == 0x4u) {
        mmio_phys |= ((uint64_t)bar1 << 32);
    }
    if (mmio_phys == 0u) {
        return RDNX_E_INVALID;
    }

    mmio_virt = XHCI_MMIO_VIRT_BASE + ((uint64_t)slot->slot_index * XHCI_MMIO_SIZE);
    for (uint64_t off = 0; off < XHCI_MMIO_SIZE; off += PAGE_SIZE) {
        if (paging_map_page_4kb(mmio_virt + off, mmio_phys + off, mmio_flags) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
    }

    slot->xhci.mmio_phys = mmio_phys;
    slot->xhci.mmio_base = (volatile uint8_t*)(uintptr_t)mmio_virt;
    return RDNX_OK;
}

int usb_xhci_init(usb_host_slot_t* slot)
{
    uint32_t hcsparams1;
    uint32_t usbcmd;

    if (!slot) {
        return RDNX_E_INVALID;
    }
    if (usb_xhci_map_mmio(slot) != RDNX_OK) {
        return RDNX_E_GENERIC;
    }

    slot->xhci.caplength = *(volatile uint8_t*)(slot->xhci.mmio_base + XHCI_CAPLENGTH);
    slot->xhci.op_base = slot->xhci.caplength;
    hcsparams1 = usb_xhci_rd32(&slot->xhci, XHCI_HCSPARAMS1);
    slot->xhci.max_slots = hcsparams1 & 0xFFu;
    slot->xhci.port_count = (uint8_t)((hcsparams1 >> 24) & 0xFFu);
    slot->xhci.dboff = usb_xhci_rd32(&slot->xhci, XHCI_DBOFF) & ~0x3u;
    slot->xhci.rtsoff = usb_xhci_rd32(&slot->xhci, XHCI_RTSOFF) & ~0x1Fu;

    if (usb_xhci_wait_for(&slot->xhci,
                          (uint32_t)(slot->xhci.caplength + XHCI_USBSTS),
                          XHCI_USBSTS_HCH,
                          true) != RDNX_OK) {
        usbcmd = usb_xhci_rd32(&slot->xhci, (uint32_t)(slot->xhci.caplength + XHCI_USBCMD));
        usbcmd &= ~XHCI_USBCMD_RUN;
        usb_xhci_wr32(&slot->xhci, (uint32_t)(slot->xhci.caplength + XHCI_USBCMD), usbcmd);
        (void)usb_xhci_wait_for(&slot->xhci,
                                (uint32_t)(slot->xhci.caplength + XHCI_USBSTS),
                                XHCI_USBSTS_HCH,
                                true);
    }

    usbcmd = usb_xhci_rd32(&slot->xhci, (uint32_t)(slot->xhci.caplength + XHCI_USBCMD));
    usb_xhci_wr32(&slot->xhci,
                  (uint32_t)(slot->xhci.caplength + XHCI_USBCMD),
                  usbcmd | XHCI_USBCMD_HCRST);
    if (usb_xhci_wait_for(&slot->xhci,
                          (uint32_t)(slot->xhci.caplength + XHCI_USBCMD),
                          XHCI_USBCMD_HCRST,
                          false) != RDNX_OK) {
        return RDNX_E_TIMEOUT;
    }
    if (usb_xhci_wait_for(&slot->xhci,
                          (uint32_t)(slot->xhci.caplength + XHCI_USBSTS),
                          XHCI_USBSTS_CNR,
                          false) != RDNX_OK) {
        return RDNX_E_TIMEOUT;
    }

    if (usb_xhci_setup_rings(slot) != RDNX_OK) {
        return RDNX_E_NOMEM;
    }

    usb_xhci_wr32(&slot->xhci,
                  slot->xhci.op_base + XHCI_CONFIG,
                  slot->xhci.max_slots);
    usb_xhci_wr32(&slot->xhci,
                  slot->xhci.op_base + XHCI_USBCMD,
                  usb_xhci_rd32(&slot->xhci, slot->xhci.op_base + XHCI_USBCMD) | XHCI_USBCMD_RUN);
    if (usb_xhci_wait_for(&slot->xhci,
                          slot->xhci.op_base + XHCI_USBSTS,
                          XHCI_USBSTS_HCH,
                          false) != RDNX_OK) {
        return RDNX_E_TIMEOUT;
    }

    slot->xhci.initialized = 1u;
    slot->xhci.running = 1u;

    (void)usb_xhci_noop(slot);

    fabric_log("[USB] xHCI ready: %s ports=%u slots=%u caplen=%u pagesz=%x crcr=%llx\n",
               slot->name,
               slot->xhci.port_count,
               slot->xhci.max_slots,
               slot->xhci.caplength,
               usb_xhci_rd32(&slot->xhci, slot->xhci.op_base + XHCI_PAGESIZE),
               (unsigned long long)usb_xhci_rd64(&slot->xhci, slot->xhci.op_base + XHCI_CRCR_LO));
    return RDNX_OK;
}

uint32_t usb_xhci_portsc(const usb_host_slot_t* slot, uint32_t port)
{
    if (!slot || port == 0u || port > slot->xhci.port_count || !slot->xhci.mmio_base) {
        return 0u;
    }
    return usb_xhci_rd32(&slot->xhci,
                         (uint32_t)(slot->xhci.caplength + XHCI_PORTREG_BASE + ((port - 1u) * XHCI_PORTREG_SIZE)));
}

uint8_t usb_xhci_port_speed(const usb_host_slot_t* slot, uint32_t port)
{
    uint32_t portsc = usb_xhci_portsc(slot, port);
    return (uint8_t)((portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT);
}

int usb_xhci_port_connected(const usb_host_slot_t* slot, uint32_t port)
{
    return (usb_xhci_portsc(slot, port) & XHCI_PORTSC_CCS) != 0u;
}
