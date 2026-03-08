#ifndef _RODNIX_USERLAND_NETIF_H
#define _RODNIX_USERLAND_NETIF_H

#include <stdint.h>

typedef struct netif_stats {
    uint64_t tx_frames;
    uint64_t tx_bytes;
    uint64_t rx_frames;
    uint64_t rx_bytes;
    uint64_t drops;
} netif_stats_t;

typedef struct netif_info {
    char name[16];
    uint8_t mac[6];
    uint32_t mtu;
    uint32_t flags;
    netif_stats_t stats;
} netif_info_t;

#endif /* _RODNIX_USERLAND_NETIF_H */
