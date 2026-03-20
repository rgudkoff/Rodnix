/**
 * @file usb_msc.c
 * @brief USB Mass Storage Class driver (Bulk-Only Transport + SCSI)
 */

#include "usb_msc.h"
#include "../../../kernel/usb/usb_proto.h"
#include "../../../kernel/usb/usb.h"
#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/fabric/service/block_service.h"
#include "../storage/mbr_part.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include "../../../trace/bootlog.h"
#include <stdbool.h>
#include <stdint.h>

#define USB_MSC_EP_BUF_SIZE 512u

/* Bulk-Only Transport */
#define USB_BOT_CBW_SIGNATURE 0x43425355u
#define USB_BOT_CSW_SIGNATURE 0x53425355u
#define USB_BOT_CBW_SIZE      31u
#define USB_BOT_CSW_SIZE      13u
#define USB_BOT_DIR_IN        0x80u
#define USB_BOT_DIR_OUT       0x00u

/* SCSI commands */
#define SCSI_INQUIRY          0x12u
#define SCSI_READ_CAPACITY_10 0x25u
#define SCSI_READ_10          0x28u
#define SCSI_WRITE_10         0x2Au

#define USB_MSC_SLOT_MAX      4u
#define USB_MSC_NAME_LEN      8u

typedef struct __attribute__((packed)) usb_bot_cbw {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} usb_bot_cbw_t;

typedef struct __attribute__((packed)) usb_bot_csw {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} usb_bot_csw_t;

typedef struct {
    int used;
    int present;
    fabric_device_t* dev;
    usb_host_controller_t* host;
    usb_port_device_info_t* port_info;
    usb_ep_handle_t bulk_in;
    usb_ep_handle_t bulk_out;
    uint32_t tag;
    uint64_t sector_count;
    uint32_t sector_size;
    char disk_name[USB_MSC_NAME_LEN];
    fabric_blockdev_t blockdev;
    fabric_blockdev_ops_t blockops;
} usb_msc_slot_t;

static usb_msc_slot_t g_msc_slots[USB_MSC_SLOT_MAX];
static uint32_t g_msc_count = 0u;

static int usb_msc_send_cbw(usb_msc_slot_t* msc, uint32_t data_len,
                             uint8_t dir, const uint8_t* cdb, uint8_t cdb_len)
{
    usb_bot_cbw_t cbw;

    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = USB_BOT_CBW_SIGNATURE;
    cbw.dCBWTag = ++msc->tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = dir;
    cbw.bCBWLUN = 0u;
    cbw.bCBWCBLength = (cdb_len > 16u) ? 16u : cdb_len;
    if (cdb && cdb_len > 0u) {
        memcpy(cbw.CBWCB, cdb, cbw.bCBWCBLength);
    }
    return msc->host->ops->transfer_out(msc->host, msc->port_info,
                                        msc->bulk_out, &cbw, USB_BOT_CBW_SIZE);
}

static int usb_msc_recv_csw(usb_msc_slot_t* msc)
{
    usb_bot_csw_t csw;
    uint16_t actual = 0u;
    int rc;

    memset(&csw, 0, sizeof(csw));
    rc = msc->host->ops->transfer_in(msc->host, msc->port_info,
                                     msc->bulk_in, &csw, USB_BOT_CSW_SIZE, &actual);
    if (rc != RDNX_OK) {
        return rc;
    }
    if (csw.dCSWSignature != USB_BOT_CSW_SIGNATURE) {
        return RDNX_E_GENERIC;
    }
    if (csw.dCSWTag != msc->tag) {
        return RDNX_E_GENERIC;
    }
    if (csw.bCSWStatus != 0u) {
        return RDNX_E_GENERIC;
    }
    return RDNX_OK;
}

static int usb_msc_inquiry(usb_msc_slot_t* msc)
{
    uint8_t cdb[6] = { SCSI_INQUIRY, 0u, 0u, 0u, 36u, 0u };
    uint8_t buf[36];
    uint16_t actual = 0u;
    int rc;

    rc = usb_msc_send_cbw(msc, 36u, USB_BOT_DIR_IN, cdb, 6u);
    if (rc != RDNX_OK) {
        return rc;
    }
    memset(buf, 0, sizeof(buf));
    rc = msc->host->ops->transfer_in(msc->host, msc->port_info,
                                     msc->bulk_in, buf, 36u, &actual);
    if (rc != RDNX_OK) {
        return rc;
    }
    return usb_msc_recv_csw(msc);
}

static int usb_msc_read_capacity(usb_msc_slot_t* msc)
{
    uint8_t cdb[10] = { SCSI_READ_CAPACITY_10, 0u,0u,0u,0u,0u,0u,0u,0u,0u };
    uint8_t buf[8];
    uint16_t actual = 0u;
    int rc;

    rc = usb_msc_send_cbw(msc, 8u, USB_BOT_DIR_IN, cdb, 10u);
    if (rc != RDNX_OK) {
        return rc;
    }
    memset(buf, 0, sizeof(buf));
    rc = msc->host->ops->transfer_in(msc->host, msc->port_info,
                                     msc->bulk_in, buf, 8u, &actual);
    if (rc != RDNX_OK) {
        return rc;
    }
    rc = usb_msc_recv_csw(msc);
    if (rc != RDNX_OK) {
        return rc;
    }

    uint32_t last_lba  = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                         ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
    uint32_t block_len = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                         ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7];
    if (block_len == 0u) {
        block_len = 512u;
    }
    msc->sector_count = (uint64_t)last_lba + 1u;
    msc->sector_size  = block_len;
    return RDNX_OK;
}

static int usb_msc_block_read(fabric_blockdev_t* bdev, uint64_t lba,
                               uint32_t count, void* out)
{
    usb_msc_slot_t* msc;
    uint8_t* dst;
    uint64_t cur;
    uint32_t remain;

    if (!bdev || !out || count == 0u) {
        return RDNX_E_INVALID;
    }
    msc = (usb_msc_slot_t*)bdev->context;
    if (!msc || !msc->present) {
        return RDNX_E_NOTFOUND;
    }

    dst = (uint8_t*)out;
    cur = lba;
    remain = count;

    while (remain > 0u) {
        uint32_t batch = 1u;
        uint32_t data_len = batch * msc->sector_size;
        uint8_t cdb[10];
        uint16_t actual = 0u;
        int rc;

        if (data_len > USB_MSC_EP_BUF_SIZE) {
            batch = USB_MSC_EP_BUF_SIZE / msc->sector_size;
            if (batch == 0u) {
                batch = 1u;
            }
            data_len = batch * msc->sector_size;
        }

        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_READ_10;
        cdb[2] = (uint8_t)((cur >> 24) & 0xFFu);
        cdb[3] = (uint8_t)((cur >> 16) & 0xFFu);
        cdb[4] = (uint8_t)((cur >> 8)  & 0xFFu);
        cdb[5] = (uint8_t)( cur        & 0xFFu);
        cdb[7] = (uint8_t)((batch >> 8) & 0xFFu);
        cdb[8] = (uint8_t)( batch       & 0xFFu);

        rc = usb_msc_send_cbw(msc, data_len, USB_BOT_DIR_IN, cdb, 10u);
        if (rc != RDNX_OK) {
            return rc;
        }
        rc = msc->host->ops->transfer_in(msc->host, msc->port_info,
                                         msc->bulk_in, dst,
                                         (uint16_t)data_len, &actual);
        if (rc != RDNX_OK) {
            return rc;
        }
        rc = usb_msc_recv_csw(msc);
        if (rc != RDNX_OK) {
            return rc;
        }

        dst    += data_len;
        cur    += batch;
        remain -= batch;
    }
    return RDNX_OK;
}

static int usb_msc_block_write(fabric_blockdev_t* bdev, uint64_t lba,
                                uint32_t count, const void* in)
{
    usb_msc_slot_t* msc;
    const uint8_t* src;
    uint64_t cur;
    uint32_t remain;

    if (!bdev || !in || count == 0u) {
        return RDNX_E_INVALID;
    }
    msc = (usb_msc_slot_t*)bdev->context;
    if (!msc || !msc->present) {
        return RDNX_E_NOTFOUND;
    }

    src    = (const uint8_t*)in;
    cur    = lba;
    remain = count;

    while (remain > 0u) {
        uint32_t batch = 1u;
        uint32_t data_len = batch * msc->sector_size;
        uint8_t cdb[10];
        int rc;

        if (data_len > USB_MSC_EP_BUF_SIZE) {
            batch = USB_MSC_EP_BUF_SIZE / msc->sector_size;
            if (batch == 0u) {
                batch = 1u;
            }
            data_len = batch * msc->sector_size;
        }

        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_WRITE_10;
        cdb[2] = (uint8_t)((cur >> 24) & 0xFFu);
        cdb[3] = (uint8_t)((cur >> 16) & 0xFFu);
        cdb[4] = (uint8_t)((cur >> 8)  & 0xFFu);
        cdb[5] = (uint8_t)( cur        & 0xFFu);
        cdb[7] = (uint8_t)((batch >> 8) & 0xFFu);
        cdb[8] = (uint8_t)( batch       & 0xFFu);

        rc = usb_msc_send_cbw(msc, data_len, USB_BOT_DIR_OUT, cdb, 10u);
        if (rc != RDNX_OK) {
            return rc;
        }
        rc = msc->host->ops->transfer_out(msc->host, msc->port_info,
                                          msc->bulk_out, src, (uint16_t)data_len);
        if (rc != RDNX_OK) {
            return rc;
        }
        rc = usb_msc_recv_csw(msc);
        if (rc != RDNX_OK) {
            return rc;
        }

        src    += data_len;
        cur    += batch;
        remain -= batch;
    }
    return RDNX_OK;
}

static bool usb_msc_probe(fabric_device_t* dev)
{
    usb_port_device_info_t* info;

    if (!dev || !dev->bus_private) {
        return false;
    }
    info = (usb_port_device_info_t*)dev->bus_private;
    return (info->interface_class == USB_CLASS_MSC &&
            info->interface_subclass == USB_MSC_SUBCLASS_SCSI &&
            info->interface_protocol == USB_MSC_PROTOCOL_BOT);
}

static int usb_msc_match_score(fabric_device_t* dev)
{
    return usb_msc_probe(dev) ? FABRIC_MATCH_DEVICE_EXACT : FABRIC_MATCH_NONE;
}

static void usb_msc_make_name(char* out, size_t len, uint32_t idx)
{
    static const char prefix[] = "udisk";
    char digits[4];
    uint32_t dc = 0u;
    size_t pos = 0u;
    uint32_t n = idx;

    for (size_t i = 0u; prefix[i] != '\0' && pos + 1u < len; i++) {
        out[pos++] = prefix[i];
    }
    do {
        digits[dc++] = (char)('0' + (n % 10u));
        n /= 10u;
    } while (n != 0u && dc < ARRAY_SIZE(digits));
    while (dc > 0u && pos + 1u < len) {
        out[pos++] = digits[--dc];
    }
    if (pos < len) {
        out[pos] = '\0';
    }
}

static int usb_msc_attach(fabric_device_t* dev)
{
    usb_msc_slot_t* msc;
    usb_host_controller_t* host;
    usb_port_device_info_t* info;
    usb_ep_handle_t bulk_in;
    usb_ep_handle_t bulk_out;
    uint32_t i;

    if (!dev || !dev->bus_private) {
        return RDNX_E_INVALID;
    }
    for (i = 0u; i < USB_MSC_SLOT_MAX; i++) {
        if (!g_msc_slots[i].used) {
            break;
        }
    }
    if (i >= USB_MSC_SLOT_MAX) {
        return RDNX_E_BUSY;
    }

    info = (usb_port_device_info_t*)dev->bus_private;
    host = info->host;
    if (!host || !host->ops || !host->ops->find_endpoint) {
        return RDNX_E_NOTFOUND;
    }

    bulk_in  = host->ops->find_endpoint(host, info, USB_EP_TYPE_BULK, true);
    bulk_out = host->ops->find_endpoint(host, info, USB_EP_TYPE_BULK, false);
    if (!bulk_in || !bulk_out) {
        klog("usb-msc", "missing bulk endpoints for port%u\n", info->port_number);
        return RDNX_E_NOTFOUND;
    }

    msc = &g_msc_slots[i];
    memset(msc, 0, sizeof(*msc));
    msc->used = 1;
    msc->dev = dev;
    msc->host = host;
    msc->port_info = info;
    msc->bulk_in  = bulk_in;
    msc->bulk_out = bulk_out;
    msc->sector_size = 512u;
    usb_msc_make_name(msc->disk_name, sizeof(msc->disk_name), g_msc_count++);

    (void)usb_msc_inquiry(msc);

    if (usb_msc_read_capacity(msc) == RDNX_OK) {
        msc->present = 1;
        klog("usb-msc", "%s: %llu sectors %u B/sec\n",
             msc->disk_name,
             (unsigned long long)msc->sector_count,
             msc->sector_size);
    } else {
        klog("usb-msc", "%s: read-capacity failed\n", msc->disk_name);
    }

    msc->blockops.hdr = RDNX_ABI_INIT(fabric_blockdev_ops_t);
    msc->blockops.read_sectors  = usb_msc_block_read;
    msc->blockops.write_sectors = usb_msc_block_write;
    msc->blockdev.hdr = RDNX_ABI_INIT(fabric_blockdev_t);
    msc->blockdev.name         = msc->disk_name;
    msc->blockdev.sector_size  = msc->sector_size;
    msc->blockdev.sector_count = msc->sector_count;
    msc->blockdev.flags        = 0u;
    msc->blockdev.ops          = &msc->blockops;
    msc->blockdev.context      = msc;

    klog("usb-msc", "attached: %s port%u\n", msc->disk_name, info->port_number);
    return RDNX_OK;
}

static int usb_msc_publish(fabric_device_t* dev)
{
    uint32_t i;

    for (i = 0u; i < USB_MSC_SLOT_MAX; i++) {
        if (!g_msc_slots[i].used || g_msc_slots[i].dev != dev) {
            continue;
        }
        if (fabric_publish_service_node(g_msc_slots[i].disk_name, "storage", dev) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
        if (g_msc_slots[i].present) {
            (void)fabric_blockdev_register(&g_msc_slots[i].blockdev);
            (void)mbr_scan_and_register(&g_msc_slots[i].blockdev);
        }
        return RDNX_OK;
    }
    return RDNX_E_NOTFOUND;
}

static void usb_msc_detach(fabric_device_t* dev)
{
    uint32_t i;
    for (i = 0u; i < USB_MSC_SLOT_MAX; i++) {
        if (g_msc_slots[i].used && g_msc_slots[i].dev == dev) {
            g_msc_slots[i].used = 0;
            return;
        }
    }
}

static fabric_driver_t g_usb_msc_driver = {
    .name = "usb-msc",
    .match_properties = NULL,
    .match_property_count = 0,
    .match_score = usb_msc_match_score,
    .probe = usb_msc_probe,
    .attach = usb_msc_attach,
    .publish = usb_msc_publish,
    .detach = usb_msc_detach,
    .suspend = NULL,
    .resume = NULL
};

void usb_msc_init(void)
{
    int rc = fabric_driver_register(&g_usb_msc_driver);
    if (rc == RDNX_OK) {
        klog("usb-msc", "driver registered\n");
    } else {
        klog_warn("usb-msc", "driver registration failed\n");
    }
}
