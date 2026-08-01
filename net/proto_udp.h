#ifndef _RODNIX_PROTO_UDP_H
#define _RODNIX_PROTO_UDP_H

/*
 * UDP datagram encapsulation and decapsulation.
 *
 * Pure wire-format code: builds a complete Ethernet/IP/UDP frame for
 * transmission, and validates/parses a received one. No socket state.
 */

#include <stddef.h>
#include <stdint.h>

#include "socket.h"
#include "bsd_ether.h"
#include "../fabric/service/net_service.h"

/* Build and transmit one UDP datagram. Returns bytes sent, or -1. */
int udp_proto_send(fabric_netif_t* iface,
                   uint32_t src_host, uint16_t src_port,
                   uint32_t dst_host, uint16_t dst_port,
                   const uint8_t src_mac[BSD_ETHER_ADDR_LEN],
                   const uint8_t dst_mac[BSD_ETHER_ADDR_LEN],
                   const void* buf, size_t len);

/*
 * Validate a received Ethernet/IP/UDP frame and locate its payload.
 * Checks the IP and UDP checksums and every length field.
 * Returns 0 on success, -1 if the frame is malformed or corrupt.
 */
int udp_proto_parse(const void* frame, size_t frame_len,
                    const void** payload_out, size_t* payload_len_out,
                    sockaddr_in_t* src_out);

#endif /* _RODNIX_PROTO_UDP_H */
