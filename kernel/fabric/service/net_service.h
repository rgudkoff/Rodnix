/**
 * @file net_service.h
 * @brief Fabric network service (device/service-facing NIC registry)
 */

#ifndef _RODNIX_FABRIC_NET_SERVICE_H
#define _RODNIX_FABRIC_NET_SERVICE_H

#include "../../../include/abi.h"
#include <stdint.h>
#include <stdbool.h>

#define FABRIC_NETIF_MAX 16
#define FABRIC_NET_FRAME_MAX 2048

enum {
    FABRIC_NETIF_F_UP       = 1u << 0,
    FABRIC_NETIF_F_LOOPBACK = 1u << 1,
    FABRIC_NETIF_F_BROADCAST = 1u << 2
};

typedef struct fabric_netif fabric_netif_t;

typedef struct fabric_netif_ops {
    rdnx_abi_header_t hdr;
    int (*tx)(fabric_netif_t* iface, const void* frame, uint32_t len);
} fabric_netif_ops_t;

typedef struct fabric_netif_stats {
    uint64_t tx_frames;
    uint64_t tx_bytes;
    uint64_t rx_frames;
    uint64_t rx_bytes;
    uint64_t drops;
} fabric_netif_stats_t;

typedef struct fabric_netif_info {
    char name[16];
    uint8_t mac[6];
    uint32_t mtu;
    uint32_t flags;
    fabric_netif_stats_t stats;
} fabric_netif_info_t;

struct fabric_netif {
    rdnx_abi_header_t hdr;
    const char* name;
    uint8_t mac[6];
    uint32_t mtu;
    uint32_t flags;
    const fabric_netif_ops_t* ops;
    void* context;
    fabric_netif_stats_t stats;
};

int fabric_net_service_init(void);
int fabric_netif_register(fabric_netif_t* iface);
uint32_t fabric_netif_count(void);
fabric_netif_t* fabric_netif_get(uint32_t index);
int fabric_netif_tx(fabric_netif_t* iface, const void* frame, uint32_t len);
int fabric_netif_rx_submit(fabric_netif_t* iface, const void* frame, uint32_t len);
int fabric_netif_get_info(uint32_t index, fabric_netif_info_t* out);

#endif /* _RODNIX_FABRIC_NET_SERVICE_H */
