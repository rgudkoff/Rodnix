#ifndef _RODNIX_NET_SOCKET_H
#define _RODNIX_NET_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

#define NET_LOOPBACK_ADDR 0x7F000001u

typedef struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
} sockaddr_in_t;

typedef struct net_socket net_socket_t;

int net_ingress_frame(const void* frame, uint32_t len, void* ifp_hint);

net_socket_t* net_socket_create(int domain, int type, int protocol);
int net_socket_bind(net_socket_t* sock, const sockaddr_in_t* addr);
int net_socket_connect(net_socket_t* sock, const sockaddr_in_t* addr);
int net_socket_sendto(net_socket_t* sock, const void* buf, size_t len, const sockaddr_in_t* dst);
int net_socket_recvfrom(net_socket_t* sock, void* buf, size_t len, sockaddr_in_t* src, uint64_t timeout_ms);
int net_socket_send(net_socket_t* sock, const void* buf, size_t len);
int net_socket_recv(net_socket_t* sock, void* buf, size_t len, uint64_t timeout_ms);
void net_socket_close(net_socket_t* sock);
int net_ping_ipv4(uint32_t dst_host, uint32_t timeout_ms, uint32_t* out_rtt_ms);

#endif /* _RODNIX_NET_SOCKET_H */
