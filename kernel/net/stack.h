#ifndef _RODNIX_NET_STACK_H
#define _RODNIX_NET_STACK_H

#include "../../include/error.h"
#include "../fabric/service/net_service.h"
#include "bsd_ether.h"
#include <stddef.h>
#include <stdint.h>

typedef enum net_stack_l4_proto {
    NET_STACK_L4_ICMP = 1u,
    NET_STACK_L4_TCP  = 6u,
    NET_STACK_L4_UDP  = 17u,
} net_stack_l4_proto_t;

typedef struct net_stack_frame {
    const void* data;
    uint32_t len;
    void* ifp_hint;
} net_stack_frame_t;

typedef struct net_stack_route {
    fabric_netif_t* iface;
    uint32_t src_host;
    uint32_t dst_host;
    uint32_t next_hop_host;
    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    int mac_resolved;
} net_stack_route_t;

typedef struct net_stack_ipv4_msg {
    uint32_t src_host;
    uint32_t dst_host;
    uint8_t ttl;
    uint8_t protocol;
    const void* payload;
    size_t payload_len;
} net_stack_ipv4_msg_t;

typedef struct net_stack_udp_msg {
    uint32_t src_host;
    uint32_t dst_host;
    uint16_t src_port;
    uint16_t dst_port;
    const void* payload;
    size_t payload_len;
} net_stack_udp_msg_t;

typedef struct net_stack_tcp_msg {
    uint32_t src_host;
    uint32_t dst_host;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint16_t window;
    uint8_t flags;
    const void* payload;
    size_t payload_len;
} net_stack_tcp_msg_t;

int net_stack_bootstrap(void);
int net_stack_poll(void);
int net_stack_ingress(const net_stack_frame_t* frame);

int net_stack_route_lookup(uint32_t dst_host, net_stack_route_t* out_route);
int net_stack_neighbor_resolve(net_stack_route_t* route);

int net_stack_ether_input(const net_stack_frame_t* frame);
int net_stack_ether_output(net_stack_route_t* route, const void* frame, uint32_t len);

int net_stack_arp_input(const net_stack_frame_t* frame);
int net_stack_arp_request(uint32_t target_ip);
int net_stack_arp_reply(const net_stack_route_t* route,
                        uint32_t target_ip,
                        const uint8_t target_mac[BSD_ETHER_ADDR_LEN]);

int net_stack_ipv4_input(const net_stack_frame_t* frame);
int net_stack_ipv4_output(const net_stack_ipv4_msg_t* msg);
int net_stack_ipv4_forward(const net_stack_frame_t* frame);

int net_stack_icmp_input(const net_stack_frame_t* frame);
int net_stack_icmp_echo(uint32_t dst_host, uint32_t timeout_ms, uint32_t* out_rtt_ms);

int net_stack_udp_input(const net_stack_frame_t* frame);
int net_stack_udp_output(const net_stack_udp_msg_t* msg);

int net_stack_tcp_input(const net_stack_frame_t* frame);
int net_stack_tcp_output(const net_stack_tcp_msg_t* msg);
int net_stack_tcp_listen(uint16_t port, int backlog);
int net_stack_tcp_connect(uint32_t dst_host, uint16_t dst_port, uint64_t timeout_ms);
int net_stack_tcp_close(uint16_t local_port, uint32_t remote_ip, uint16_t remote_port);

#endif /* _RODNIX_NET_STACK_H */
