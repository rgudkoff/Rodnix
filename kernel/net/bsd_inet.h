#ifndef _RODNIX_BSD_INET_H
#define _RODNIX_BSD_INET_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal IPv4/UDP wire definitions and checksum helpers.
 * Layout and naming are compatible with classic BSD networking headers.
 */

#define BSD_IPPROTO_ICMP 1u
#define BSD_IPPROTO_UDP 17u
#define BSD_IPVERSION   4u
#define BSD_IP_DF       0x4000u
#define BSD_IP_TTL_DEF  64u

#define BSD_ICMP_ECHOREPLY 0u
#define BSD_ICMP_ECHO      8u

typedef struct bsd_ip {
    uint8_t ip_vhl;      /* version << 4 | header_len_words */
    uint8_t ip_tos;
    uint16_t ip_len;     /* total length, network order */
    uint16_t ip_id;      /* identification, network order */
    uint16_t ip_off;     /* flags/fragment offset, network order */
    uint8_t ip_ttl;
    uint8_t ip_p;        /* protocol */
    uint16_t ip_sum;     /* header checksum, network order */
    uint32_t ip_src;     /* source address, network order */
    uint32_t ip_dst;     /* destination address, network order */
} __attribute__((packed)) bsd_ip_t;

typedef struct bsd_udphdr {
    uint16_t uh_sport;   /* source port, network order */
    uint16_t uh_dport;   /* destination port, network order */
    uint16_t uh_ulen;    /* udp length, network order */
    uint16_t uh_sum;     /* checksum, network order */
} __attribute__((packed)) bsd_udphdr_t;

typedef struct bsd_icmp_echo {
    uint8_t type;
    uint8_t code;
    uint16_t cksum;      /* network order */
    uint16_t id;         /* network order */
    uint16_t seq;        /* network order */
} __attribute__((packed)) bsd_icmp_echo_t;

uint16_t bsd_htons(uint16_t v);
uint16_t bsd_ntohs(uint16_t v);
uint32_t bsd_htonl(uint32_t v);
uint32_t bsd_ntohl(uint32_t v);

uint16_t bsd_in_cksum(const void* data, size_t len);
uint16_t bsd_icmp_checksum(const void* data, size_t len);
uint16_t bsd_in_pseudo(uint32_t src_host, uint32_t dst_host, uint16_t proto_len_host);
uint16_t bsd_udp4_checksum(uint32_t src_host, uint32_t dst_host, const bsd_udphdr_t* uh, const void* payload, size_t payload_len);

#endif /* _RODNIX_BSD_INET_H */
