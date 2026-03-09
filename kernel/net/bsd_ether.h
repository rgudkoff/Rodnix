#ifndef _RODNIX_BSD_ETHER_H
#define _RODNIX_BSD_ETHER_H

#include <stddef.h>
#include <stdint.h>

#define BSD_ETHER_ADDR_LEN 6u
#define BSD_ETHER_HDR_LEN 14u

#define BSD_ETHERTYPE_IP   0x0800u
#define BSD_ETHERTYPE_ARP  0x0806u

#define BSD_ARPHRD_ETHER   1u
#define BSD_ARPOP_REQUEST  1u
#define BSD_ARPOP_REPLY    2u

typedef struct bsd_ether_header {
    uint8_t  ether_dhost[BSD_ETHER_ADDR_LEN];
    uint8_t  ether_shost[BSD_ETHER_ADDR_LEN];
    uint16_t ether_type; /* network order */
} __attribute__((packed)) bsd_ether_header_t;

typedef struct bsd_arp_eth_ipv4 {
    uint16_t htype;       /* network order */
    uint16_t ptype;       /* network order */
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;        /* network order */
    uint8_t  sha[BSD_ETHER_ADDR_LEN];
    uint32_t spa;         /* network order */
    uint8_t  tha[BSD_ETHER_ADDR_LEN];
    uint32_t tpa;         /* network order */
} __attribute__((packed)) bsd_arp_eth_ipv4_t;

void bsd_arp_init(void);
int bsd_arp_add(uint32_t ip_host, const uint8_t mac[BSD_ETHER_ADDR_LEN]);
int bsd_arp_lookup(uint32_t ip_host, uint8_t mac_out[BSD_ETHER_ADDR_LEN]);

#endif /* _RODNIX_BSD_ETHER_H */
