/*
 * proto_udp.c — UDP datagram encapsulation and decapsulation.
 *
 * Wire-format code only. udp_proto_send() builds a complete frame and hands
 * it to the interface; udp_proto_parse() validates a received frame down to
 * the UDP checksum and reports where the payload is. Neither touches socket
 * state, so both are drivable from a test harness with a synthetic frame.
 */

#include "proto_udp.h"
#include "net_proto.h"
#include "socket.h"
#include "bsd_inet.h"
#include "bsd_ether.h"
#include "../kernel/fabric/service/net_service.h"
#include "../lib/heap.h"
#include "../include/common.h"
#include "../include/error.h"

int udp_proto_send(fabric_netif_t* iface,
                   uint32_t src_host, uint16_t src_port,
                   uint32_t dst_host, uint16_t dst_port,
                   const uint8_t src_mac[BSD_ETHER_ADDR_LEN],
                   const uint8_t dst_mac[BSD_ETHER_ADDR_LEN],
                   const void* buf, size_t len)
{
    if (!iface || !src_mac || !dst_mac || !buf) {
        return -1;
    }
    if (len == 0 || len > (size_t)(0xFFFFu - sizeof(bsd_udphdr_t))) {
        return -1;
    }

    const size_t eth_len = sizeof(bsd_ether_header_t);
    const size_t ip_len = sizeof(bsd_ip_t);
    const size_t udp_len = sizeof(bsd_udphdr_t) + len;
    const size_t frame_len = eth_len + ip_len + udp_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return -1;
    }
    memset(frame, 0, frame_len);

    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_shost, src_mac, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_dhost, dst_mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    bsd_ip_t* ip = (bsd_ip_t*)(frame + eth_len);
    ip->ip_vhl = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos = 0;
    ip->ip_len = bsd_htons((uint16_t)(ip_len + udp_len));
    ip->ip_id = 0;
    ip->ip_off = bsd_htons(BSD_IP_DF);
    ip->ip_ttl = BSD_IP_TTL_DEF;
    ip->ip_p = BSD_IPPROTO_UDP;
    ip->ip_src = bsd_htonl(src_host);
    ip->ip_dst = bsd_htonl(dst_host);
    ip->ip_sum = 0;
    ip->ip_sum = bsd_htons(bsd_in_cksum(ip, sizeof(*ip)));

    bsd_udphdr_t* uh = (bsd_udphdr_t*)(frame + eth_len + ip_len);
    uh->uh_sport = bsd_htons(src_port);
    uh->uh_dport = bsd_htons(dst_port);
    uh->uh_ulen = bsd_htons((uint16_t)udp_len);
    uh->uh_sum = 0;
    memcpy((uint8_t*)(uh + 1), buf, len);
    uh->uh_sum = bsd_htons(bsd_udp4_checksum(src_host, dst_host, uh,
                                             (const void*)(uh + 1), len));

    int tx_rc = fabric_netif_tx(iface, frame, (uint32_t)frame_len);
    kfree(frame);
    return (tx_rc == RDNX_OK) ? (int)len : -1;
}

int udp_proto_parse(const void* frame, size_t frame_len,
                    const void** payload_out, size_t* payload_len_out,
                    sockaddr_in_t* src_out)
{
    if (!frame || !payload_out || !payload_len_out) {
        return -1;
    }

    const uint8_t* bytes = (const uint8_t*)frame;

    if (frame_len < sizeof(bsd_ether_header_t) + sizeof(bsd_ip_t) + sizeof(bsd_udphdr_t)) {
        return -1;
    }

    const bsd_ether_header_t* eh = (const bsd_ether_header_t*)bytes;
    if (bsd_ntohs(eh->ether_type) != BSD_ETHERTYPE_IP) {
        return -1;
    }

    const bsd_ip_t* ip = (const bsd_ip_t*)(bytes + sizeof(bsd_ether_header_t));
    const uint8_t ihl_words = (uint8_t)(ip->ip_vhl & 0x0Fu);
    const size_t ihl = (size_t)ihl_words * 4u;
    if (ihl < sizeof(bsd_ip_t) ||
        frame_len < sizeof(bsd_ether_header_t) + ihl + sizeof(bsd_udphdr_t)) {
        return -1;
    }

    if (bsd_in_cksum(ip, ihl) != 0) {
        return -1;
    }

    const bsd_udphdr_t* uh = (const bsd_udphdr_t*)((const uint8_t*)ip + ihl);
    const uint16_t udp_len = bsd_ntohs(uh->uh_ulen);
    if (udp_len < sizeof(bsd_udphdr_t) ||
        sizeof(bsd_ether_header_t) + ihl + udp_len > frame_len) {
        return -1;
    }

    const uint8_t* payload = (const uint8_t*)(uh + 1);
    const size_t payload_len = (size_t)udp_len - sizeof(bsd_udphdr_t);

    if (uh->uh_sum != 0) {
        uint16_t udp_sum_calc = bsd_udp4_checksum(bsd_ntohl(ip->ip_src),
                                                  bsd_ntohl(ip->ip_dst),
                                                  uh,
                                                  payload,
                                                  payload_len);
        if (bsd_htons(udp_sum_calc) != uh->uh_sum) {
            return -1;
        }
    }

    if (src_out) {
        src_out->sin_family = AF_INET;
        src_out->sin_port = bsd_ntohs(uh->uh_sport);
        src_out->sin_addr = bsd_ntohl(ip->ip_src);
    }

    *payload_out = payload;
    *payload_len_out = payload_len;
    return 0;
}
