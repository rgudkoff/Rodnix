#ifndef _RODNIX_PROTO_ICMP_H
#define _RODNIX_PROTO_ICMP_H

/*
 * ICMP echo handling and the ping engine.
 *
 * Echo requests are answered directly from the input path. Echo replies
 * update the single outstanding ping slot; net_ping_ipv4() (declared in
 * socket.h, implemented in socket.c) owns the waiting.
 */

#include <stddef.h>
#include <stdint.h>

#include "bsd_ether.h"
#include "bsd_inet.h"
#include "../fabric/service/net_service.h"

void icmp_proto_init(void);

/* Answer an inbound echo request. Returns 0 on success, -1 otherwise. */
int icmp_proto_send_echo_reply(fabric_netif_t* iface,
                               const bsd_ether_header_t* rx_eh,
                               const bsd_ip_t* rx_ip,
                               const bsd_icmp_echo_t* rx_icmp,
                               size_t icmp_len);

/* Record an inbound echo reply against the outstanding ping, if it matches. */
void icmp_proto_note_echo_reply(uint32_t src_ip, uint16_t id, uint16_t seq);

/*
 * Transmit one echo request and arm the ping slot.
 * Returns the sequence number on success, or 0 on failure.
 */
uint16_t icmp_proto_send_echo(fabric_netif_t* iface,
                              uint32_t src_host, uint32_t dst_host,
                              const uint8_t src_mac[BSD_ETHER_ADDR_LEN],
                              const uint8_t dst_mac[BSD_ETHER_ADDR_LEN]);

/*
 * Poll the ping slot for a reply matching `seq`.
 * Returns 1 and fills out_rtt_ms when it has arrived, 0 otherwise.
 */
int icmp_proto_poll_reply(uint16_t seq, uint32_t* out_rtt_ms);

/* Disarm the ping slot for `seq` (timeout or transmit failure). */
void icmp_proto_cancel(uint16_t seq);

#endif /* _RODNIX_PROTO_ICMP_H */
