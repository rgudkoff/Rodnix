#include "socket.h"
#include "../fabric/spin.h"
#include "../common/heap.h"
#include "../common/scheduler.h"
#include "../../include/console.h"
#include "../../include/common.h"

#define NET_MAX_SOCKETS 1024

struct udp_msg {
    sockaddr_in_t src;
    size_t len;
    uint8_t* data;
    struct udp_msg* next;
};

typedef struct udp_queue {
    struct udp_msg* head;
    struct udp_msg* tail;
    uint32_t count;
    spinlock_t lock;
} udp_queue_t;

struct net_socket {
    int domain;
    int type;
    int protocol;
    uint16_t bound_port;
    int bound;
    uint16_t connected_port;
    int connected;
    udp_queue_t queue;
};

static net_socket_t* udp_port_table[NET_MAX_SOCKETS];
static spinlock_t udp_port_lock;

static void udp_queue_init(udp_queue_t* q)
{
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    spinlock_init(&q->lock);
}

static int udp_queue_push(udp_queue_t* q, const sockaddr_in_t* src, const void* buf, size_t len)
{
    if (!q || !src || !buf || len == 0) {
        return -1;
    }
    struct udp_msg* msg = (struct udp_msg*)kmalloc(sizeof(struct udp_msg));
    if (!msg) {
        return -1;
    }
    msg->data = (uint8_t*)kmalloc(len);
    if (!msg->data) {
        kfree(msg);
        return -1;
    }
    memcpy(msg->data, buf, len);
    msg->len = len;
    msg->src = *src;
    msg->next = NULL;

    spinlock_lock(&q->lock);
    if (!q->tail) {
        q->head = msg;
        q->tail = msg;
    } else {
        q->tail->next = msg;
        q->tail = msg;
    }
    q->count++;
    spinlock_unlock(&q->lock);
    return 0;
}

static int udp_queue_pop(udp_queue_t* q, void* buf, size_t len, sockaddr_in_t* src)
{
    if (!q || !buf || len == 0) {
        return -1;
    }
    spinlock_lock(&q->lock);
    struct udp_msg* msg = q->head;
    if (!msg) {
        spinlock_unlock(&q->lock);
        return -1;
    }
    q->head = msg->next;
    if (!q->head) {
        q->tail = NULL;
    }
    if (q->count > 0) {
        q->count--;
    }
    spinlock_unlock(&q->lock);

    size_t to_copy = msg->len < len ? msg->len : len;
    memcpy(buf, msg->data, to_copy);
    if (src) {
        *src = msg->src;
    }
    kfree(msg->data);
    kfree(msg);
    return (int)to_copy;
}

static uint64_t udp_deadline(uint64_t timeout_ms)
{
    if (timeout_ms == 0) {
        return 0;
    }
    uint64_t now = scheduler_get_ticks();
    uint64_t ticks = (timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1)) / SCHEDULER_TIME_SLICE_MS;
    if (ticks == 0) {
        ticks = 1;
    }
    return now + ticks;
}

net_socket_t* net_socket_create(int domain, int type, int protocol)
{
    if (domain != AF_INET) {
        return NULL;
    }
    if (type != SOCK_DGRAM && type != SOCK_STREAM) {
        return NULL;
    }
    (void)protocol;

    net_socket_t* sock = (net_socket_t*)kmalloc(sizeof(net_socket_t));
    if (!sock) {
        return NULL;
    }
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->bound_port = 0;
    sock->bound = 0;
    sock->connected_port = 0;
    sock->connected = 0;
    udp_queue_init(&sock->queue);
    return sock;
}

int net_socket_bind(net_socket_t* sock, const sockaddr_in_t* addr)
{
    if (!sock || !addr) {
        return -1;
    }
    if (sock->domain != AF_INET) {
        return -1;
    }
    if (addr->sin_family != AF_INET) {
        return -1;
    }
    uint16_t port = addr->sin_port;
    if (port == 0 || port >= NET_MAX_SOCKETS) {
        return -1;
    }

    spinlock_lock(&udp_port_lock);
    if (udp_port_table[port]) {
        spinlock_unlock(&udp_port_lock);
        return -1;
    }
    udp_port_table[port] = sock;
    sock->bound_port = port;
    sock->bound = 1;
    spinlock_unlock(&udp_port_lock);
    return 0;
}

int net_socket_connect(net_socket_t* sock, const sockaddr_in_t* addr)
{
    if (!sock || !addr) {
        return -1;
    }
    if (sock->domain != AF_INET || sock->type != SOCK_STREAM) {
        return -1;
    }
    if (addr->sin_family != AF_INET || addr->sin_addr != NET_LOOPBACK_ADDR) {
        return -1;
    }
    uint16_t dport = addr->sin_port;
    if (dport == 0 || dport >= NET_MAX_SOCKETS) {
        return -1;
    }
    spinlock_lock(&udp_port_lock);
    net_socket_t* dest = udp_port_table[dport];
    spinlock_unlock(&udp_port_lock);
    if (!dest || dest->type != SOCK_STREAM) {
        return -1;
    }
    sock->connected_port = dport;
    sock->connected = 1;
    return 0;
}

int net_socket_sendto(net_socket_t* sock, const void* buf, size_t len, const sockaddr_in_t* dst)
{
    if (!sock || !buf || !dst) {
        return -1;
    }
    if (sock->domain != AF_INET || (sock->type != SOCK_DGRAM && sock->type != SOCK_STREAM)) {
        return -1;
    }
    if (dst->sin_family != AF_INET || dst->sin_addr != NET_LOOPBACK_ADDR) {
        return -1;
    }
    uint16_t dport = dst->sin_port;
    if (dport == 0 || dport >= NET_MAX_SOCKETS) {
        return -1;
    }

    spinlock_lock(&udp_port_lock);
    net_socket_t* dest = udp_port_table[dport];
    spinlock_unlock(&udp_port_lock);
    if (!dest) {
        return -1;
    }

    sockaddr_in_t src = {0};
    src.sin_family = AF_INET;
    src.sin_addr = NET_LOOPBACK_ADDR;
    src.sin_port = sock->bound ? sock->bound_port : 0;

    return udp_queue_push(&dest->queue, &src, buf, len);
}

int net_socket_send(net_socket_t* sock, const void* buf, size_t len)
{
    if (!sock || !buf) {
        return -1;
    }
    if (sock->type != SOCK_STREAM || !sock->connected) {
        return -1;
    }
    sockaddr_in_t dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_addr = NET_LOOPBACK_ADDR;
    dst.sin_port = sock->connected_port;
    return net_socket_sendto(sock, buf, len, &dst);
}

int net_socket_recvfrom(net_socket_t* sock, void* buf, size_t len, sockaddr_in_t* src, uint64_t timeout_ms)
{
    if (!sock || !buf) {
        return -1;
    }
    if (sock->domain != AF_INET || (sock->type != SOCK_DGRAM && sock->type != SOCK_STREAM)) {
        return -1;
    }

    uint64_t deadline = udp_deadline(timeout_ms);
    for (;;) {
        int ret = udp_queue_pop(&sock->queue, buf, len, src);
        if (ret >= 0) {
            return ret;
        }
        if (deadline && scheduler_get_ticks() >= deadline) {
            return -1;
        }
        scheduler_yield();
    }
}

int net_socket_recv(net_socket_t* sock, void* buf, size_t len, uint64_t timeout_ms)
{
    return net_socket_recvfrom(sock, buf, len, NULL, timeout_ms);
}

void net_socket_close(net_socket_t* sock)
{
    if (!sock) {
        return;
    }
    if (sock->bound) {
        spinlock_lock(&udp_port_lock);
        if (sock->bound_port < NET_MAX_SOCKETS && udp_port_table[sock->bound_port] == sock) {
            udp_port_table[sock->bound_port] = NULL;
        }
        spinlock_unlock(&udp_port_lock);
    }
    kfree(sock);
}
