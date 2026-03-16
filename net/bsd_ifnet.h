#ifndef _RODNIX_BSD_IFNET_H
#define _RODNIX_BSD_IFNET_H

#include <stdint.h>

#define BSD_IFNAMSIZ 16
#define BSD_IFNET_MAX 16

#define BSD_IFF_UP       0x0001u
#define BSD_IFF_RUNNING  0x0002u
#define BSD_IFF_LOOPBACK 0x0008u

typedef struct bsd_ifnet {
    char if_xname[BSD_IFNAMSIZ];
    uint32_t if_index;
    uint32_t if_flags;
    uint32_t if_mtu;
    uint8_t if_lladdr[6];
} bsd_ifnet_t;

int bsd_ifnet_attach(bsd_ifnet_t* ifp);
bsd_ifnet_t* bsd_ifnet_byindex(uint32_t ifindex);
bsd_ifnet_t* bsd_ifnet_byname(const char* ifname);

/* Compatibility aliases retained for imported driver code. */
typedef bsd_ifnet_t ifnet_t;
typedef bsd_ifnet_t ifnet;
typedef bsd_ifnet_t* if_t;

#define IFF_UP BSD_IFF_UP
#define IFF_RUNNING BSD_IFF_RUNNING
#define IFF_LOOPBACK BSD_IFF_LOOPBACK

#endif /* _RODNIX_BSD_IFNET_H */
