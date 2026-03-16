#ifndef _RODNIX_USERLAND_NETIF_H
#define _RODNIX_USERLAND_NETIF_H

#include <stddef.h>
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
    uint32_t ipv4_addr;
    uint32_t ipv4_netmask;
    uint32_t ipv4_gateway;
    netif_stats_t stats;
} netif_info_t;

_Static_assert(sizeof(netif_info_t) == 88, "netif_info_t ABI size mismatch");
_Static_assert(offsetof(netif_info_t, mtu) == 24, "netif_info_t.mtu ABI mismatch");
_Static_assert(offsetof(netif_info_t, flags) == 28, "netif_info_t.flags ABI mismatch");
_Static_assert(offsetof(netif_info_t, ipv4_addr) == 32, "netif_info_t.ipv4_addr ABI mismatch");
_Static_assert(offsetof(netif_info_t, stats) == 48, "netif_info_t.stats ABI mismatch");

#endif /* _RODNIX_USERLAND_NETIF_H */
