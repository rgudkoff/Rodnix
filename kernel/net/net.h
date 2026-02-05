#ifndef _RODNIX_NET_H
#define _RODNIX_NET_H

#include <stddef.h>
#include <stdint.h>

#define NET_MAX_PACKET 1500

typedef struct net_packet {
    uint32_t len;
    uint8_t data[NET_MAX_PACKET];
} net_packet_t;

int net_init(void);
int net_loopback_send(const net_packet_t* pkt);
int net_loopback_recv(net_packet_t* out);

#endif /* _RODNIX_NET_H */
