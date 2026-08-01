/*
 * socket.c — socket layer.
 *
 * Owns net_socket_t, the port tables, per-socket receive queues, the accept
 * queue, and every blocking loop in the network path. Wire formats and the
 * TCP state machine live in the protocol engine (proto_*.c); this file
 * reaches them through the per-protocol headers and is reached back only
 * through the net_proto_sink_t callbacks registered in socket_layer_ready().
 *
 * The split is deliberate: the engine must stay drivable without a socket
 * table so the protocol logic can be tested on its own. Keep socket state
 * out of proto_*.c.
 */

#include "socket.h"
#include "net_proto.h"
#include "proto_tcp.h"
#include "proto_udp.h"
#include "proto_icmp.h"
#include "bsd_inet.h"
#include "bsd_ether.h"
#include "stack.h"
#include "../kernel/fabric/service/net_service.h"
#include "../kernel/fabric/spin.h"
#include "../lib/heap.h"
#include "../sched/scheduler.h"
#include "../include/console.h"
#include "../include/common.h"
#include "../include/error.h"

#define NET_MAX_SOCKETS 65536

typedef struct udp_msg {
    size_t frame_len;
    uint8_t* frame;
    struct udp_msg* next;
} udp_msg_t;

typedef struct udp_queue {
    udp_msg_t* head;
    udp_msg_t* tail;
    uint32_t count;
    spinlock_t lock;
} udp_queue_t;

typedef struct net_socket {
    int domain;
    int type;
    int protocol;
    uint16_t bound_port;
    int bound;
    uint16_t connected_port;
    int connected;
    udp_queue_t queue;

    /* TCP server state */
    int         listening;
    uint32_t    accept_backlog;   /* max queue size */
    tcp_conn_t* accept_head;
    tcp_conn_t* accept_tail;
    uint32_t    accept_count;
    spinlock_t  accept_lock;

    /* TCP connection (once accepted / connected) */
    tcp_conn_t* tcp_conn;
} net_socket_t;

static net_socket_t* udp_port_table[NET_MAX_SOCKETS];
static spinlock_t udp_port_lock;

/* TCP: listening sockets indexed by local port */
static net_socket_t* g_tcp_listen_table[NET_MAX_SOCKETS];
static spinlock_t    g_tcp_listen_lock;

static int g_socket_layer_inited = 0;

static void udp_queue_init(udp_queue_t* q);
static int udp_queue_push_frame(udp_queue_t* q, const void* frame, size_t frame_len);
static int udp_queue_pop(udp_queue_t* q, void* buf, size_t len, sockaddr_in_t* src);

/* =========================================================================
 * Upcalls from the protocol engine
 * ======================================================================= */

static int sock_udp_deliver(uint16_t dport, const void* frame, size_t frame_len)
{
    spinlock_lock(&udp_port_lock);
    net_socket_t* dest = udp_port_table[dport];
    spinlock_unlock(&udp_port_lock);

    if (!dest) {
        return -1;
    }
    return udp_queue_push_frame(&dest->queue, frame, frame_len);
}

static int sock_tcp_listen_query(uint16_t port, uint32_t* out_backlog,
                                 uint32_t* out_queued)
{
    spinlock_lock(&g_tcp_listen_lock);
    net_socket_t* lsock = g_tcp_listen_table[port];
    spinlock_unlock(&g_tcp_listen_lock);

    if (!lsock || !lsock->listening) {
        return -1;
    }

    spinlock_lock(&lsock->accept_lock);
    uint32_t queued = lsock->accept_count;
    spinlock_unlock(&lsock->accept_lock);

    if (out_backlog) {
        *out_backlog = lsock->accept_backlog;
    }
    if (out_queued) {
        *out_queued = queued;
    }
    return 0;
}

static int sock_tcp_accept_enqueue(uint16_t port, tcp_conn_t* conn)
{
    if (!conn) {
        return -1;
    }

    spinlock_lock(&g_tcp_listen_lock);
    net_socket_t* lsock = g_tcp_listen_table[port];
    spinlock_unlock(&g_tcp_listen_lock);

    if (!lsock) {
        return -1;
    }

    spinlock_lock(&lsock->accept_lock);
    conn->next = NULL;
    if (!lsock->accept_tail) {
        lsock->accept_head = conn;
        lsock->accept_tail = conn;
    } else {
        lsock->accept_tail->next = conn;
        lsock->accept_tail = conn;
    }
    lsock->accept_count++;
    spinlock_unlock(&lsock->accept_lock);
    return 0;
}

static const net_proto_sink_t g_socket_sink = {
    .udp_deliver        = sock_udp_deliver,
    .tcp_listen_query   = sock_tcp_listen_query,
    .tcp_accept_enqueue = sock_tcp_accept_enqueue,
};

/*
 * Bring up the socket layer and the protocol engine below it.
 * The sink must be registered before any frame can be dispatched.
 */
static int socket_layer_ready(void)
{
    if (!g_socket_layer_inited) {
        spinlock_init(&udp_port_lock);
        spinlock_init(&g_tcp_listen_lock);
        net_proto_set_sink(&g_socket_sink);
        g_socket_layer_inited = 1;
    }
    return net_ensure_dispatch_path();
}

/* =========================================================================
 * Per-socket datagram queue
 * ======================================================================= */

static void udp_queue_init(udp_queue_t* q)
{
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    spinlock_init(&q->lock);
}

static int udp_queue_push_frame(udp_queue_t* q, const void* frame, size_t frame_len)
{
    if (!q || !frame || frame_len == 0) {
        return -1;
    }

    udp_msg_t* msg = (udp_msg_t*)kmalloc(sizeof(udp_msg_t));
    if (!msg) {
        return -1;
    }
    msg->frame = (uint8_t*)kmalloc(frame_len);
    if (!msg->frame) {
        kfree(msg);
        return -1;
    }
    memcpy(msg->frame, frame, frame_len);
    msg->frame_len = frame_len;
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
    udp_msg_t* msg = q->head;
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

    const void* payload = NULL;
    size_t payload_len = 0;
    int rc = udp_proto_parse(msg->frame, msg->frame_len,
                             &payload, &payload_len, src);

    int ret = -1;
    if (rc == 0) {
        size_t to_copy = (payload_len < len) ? payload_len : len;
        memcpy(buf, payload, to_copy);
        ret = (int)to_copy;
    }

    kfree(msg->frame);
    kfree(msg);
    return ret;
}

/* =========================================================================
 * Socket lifecycle
 * ======================================================================= */

net_socket_t* net_socket_create(int domain, int type, int protocol)
{
    if (socket_layer_ready() != 0) {
        return NULL;
    }

    if (domain != AF_INET) {
        return NULL;
    }
    if (type != SOCK_DGRAM && type != SOCK_STREAM) {
        return NULL;
    }

    net_socket_t* sock = (net_socket_t*)kmalloc(sizeof(net_socket_t));
    if (!sock) {
        return NULL;
    }

    memset(sock, 0, sizeof(*sock));
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    spinlock_init(&sock->accept_lock);
    udp_queue_init(&sock->queue);

    return sock;
}

int net_socket_bind(net_socket_t* sock, const sockaddr_in_t* addr)
{
    if (!sock || !addr) {
        return -1;
    }
    if (sock->domain != AF_INET || addr->sin_family != AF_INET) {
        return -1;
    }

    uint16_t port = addr->sin_port;
    if (port == 0) {
        return -1;
    }

    if (sock->type == SOCK_DGRAM) {
        spinlock_lock(&udp_port_lock);
        if (udp_port_table[port]) {
            spinlock_unlock(&udp_port_lock);
            return -1;
        }
        udp_port_table[port] = sock;
        spinlock_unlock(&udp_port_lock);
    }

    sock->bound_port = port;
    sock->bound = 1;

    return 0;
}

int net_socket_listen(net_socket_t* sock, int backlog)
{
    if (!sock || sock->type != SOCK_STREAM || !sock->bound) {
        return -1;
    }
    if (backlog <= 0) {
        backlog = 8;
    }

    sock->accept_backlog = (uint32_t)backlog;
    sock->listening = 1;

    spinlock_lock(&g_tcp_listen_lock);
    g_tcp_listen_table[sock->bound_port] = sock;
    spinlock_unlock(&g_tcp_listen_lock);

    return 0;
}

net_socket_t* net_socket_accept(net_socket_t* listen_sock,
                                 sockaddr_in_t* addr_out,
                                 uint64_t timeout_ms)
{
    if (!listen_sock || !listen_sock->listening) {
        return NULL;
    }

    uint64_t deadline = net_deadline_from_ms(timeout_ms);

    for (;;) {
        spinlock_lock(&listen_sock->accept_lock);
        tcp_conn_t* conn = listen_sock->accept_head;
        if (conn) {
            listen_sock->accept_head = conn->next;
            if (!listen_sock->accept_head) {
                listen_sock->accept_tail = NULL;
            }
            listen_sock->accept_count--;
            conn->next = NULL;
        }
        spinlock_unlock(&listen_sock->accept_lock);

        if (conn) {
            net_socket_t* new_sock = (net_socket_t*)kmalloc(sizeof(net_socket_t));
            if (!new_sock) {
                kfree(conn);
                return NULL;
            }
            memset(new_sock, 0, sizeof(*new_sock));
            new_sock->domain     = AF_INET;
            new_sock->type       = SOCK_STREAM;
            new_sock->bound_port = conn->local_port;
            new_sock->bound      = 1;
            new_sock->connected  = 1;
            new_sock->tcp_conn   = conn;
            udp_queue_init(&new_sock->queue);
            spinlock_init(&new_sock->accept_lock);

            if (addr_out) {
                addr_out->sin_family = AF_INET;
                addr_out->sin_port   = conn->remote_port;
                addr_out->sin_addr   = conn->remote_ip;
            }
            return new_sock;
        }

        if (deadline && scheduler_get_ticks() >= deadline) {
            return NULL;
        }
        fabric_netif_poll_all();
        scheduler_yield();
    }
}

int net_socket_connect(net_socket_t* sock, const sockaddr_in_t* addr)
{
    if (!sock || !addr) {
        return -1;
    }
    if (sock->domain != AF_INET || sock->type != SOCK_STREAM) {
        return -1;
    }
    if (addr->sin_family != AF_INET) {
        return -1;
    }

    uint16_t dport = addr->sin_port;
    if (dport == 0) {
        return -1;
    }

    /* Destination is one of our own addresses — kernel-local fast path. */
    if (net_is_local_ipv4(addr->sin_addr, NULL)) {
        spinlock_lock(&g_tcp_listen_lock);
        net_socket_t* server = g_tcp_listen_table[dport];
        spinlock_unlock(&g_tcp_listen_lock);

        if (!server || !server->listening) {
            return -1;
        }

        /* Client's ephemeral port (derived from the socket identity) */
        uint16_t cli_port = (uint16_t)(((uintptr_t)sock >> 3) & 0xFFFFu);
        if (cli_port < 1024u) {
            cli_port += 1024u;
        }

        tcp_conn_t* srv_conn = NULL;
        tcp_conn_t* cli_conn = NULL;
        if (tcp_engine_open_local_pair(dport, cli_port, addr->sin_addr,
                                       &srv_conn, &cli_conn) != 0) {
            return -1;
        }

        if (sock_tcp_accept_enqueue(dport, srv_conn) != 0) {
            kfree(srv_conn);
            kfree(cli_conn);
            return -1;
        }

        sock->tcp_conn       = cli_conn;
        sock->connected      = 1;
        sock->connected_port = dport;
        return 0;
    }

    /* Remote TCP connect: SYN -> SYN-ACK -> ACK */
    tcp_conn_t* c = tcp_engine_open(addr->sin_addr, dport);
    if (!c) {
        return -1;
    }

    uint64_t deadline = net_deadline_from_ms(TCP_CONNECT_TIMEOUT_MS);
    for (;;) {
        if (tcp_conn_state(c) == TCP_ST_ESTABLISHED) {
            sock->tcp_conn       = c;
            sock->connected      = 1;
            sock->connected_port = dport;
            sock->bound_port     = c->local_port;
            sock->bound          = 1;
            return 0;
        }

        if (scheduler_get_ticks() >= deadline) {
            tcp_engine_release(c);
            return -1;
        }

        fabric_netif_poll_all();
        scheduler_yield();
    }
}

/* =========================================================================
 * Data transfer
 * ======================================================================= */

int net_socket_sendto(net_socket_t* sock, const void* buf, size_t len,
                      const sockaddr_in_t* dst)
{
    if (!sock || !buf || !dst) {
        return -1;
    }
    if (len == 0 || len > (size_t)(0xFFFFu - 8u)) {
        return -1;
    }
    if (sock->domain != AF_INET ||
        (sock->type != SOCK_DGRAM && sock->type != SOCK_STREAM)) {
        return -1;
    }
    if (dst->sin_family != AF_INET || dst->sin_addr == 0) {
        return -1;
    }

    uint16_t dport = dst->sin_port;
    if (dport == 0) {
        return -1;
    }

    uint32_t dst_host = dst->sin_addr;
    fabric_netif_t* tx_iface = net_select_tx_iface(dst_host);
    if (!tx_iface) {
        return -1;
    }

    uint32_t src_host = 0;
    uint8_t src_mac[BSD_ETHER_ADDR_LEN];
    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    if (net_tx_params(tx_iface, dst_host, &src_host, src_mac, dst_mac) != 0) {
        return -1;
    }

    uint16_t sport = sock->bound ? sock->bound_port : 0;

    return udp_proto_send(tx_iface, src_host, sport, dst_host, dport,
                          src_mac, dst_mac, buf, len);
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

int net_socket_recvfrom(net_socket_t* sock, void* buf, size_t len,
                        sockaddr_in_t* src, uint64_t timeout_ms)
{
    if (!sock || !buf) {
        return -1;
    }
    if (sock->domain != AF_INET ||
        (sock->type != SOCK_DGRAM && sock->type != SOCK_STREAM)) {
        return -1;
    }

    uint64_t deadline = net_deadline_from_ms(timeout_ms);
    for (;;) {
        int ret = udp_queue_pop(&sock->queue, buf, len, src);
        if (ret >= 0) {
            return ret;
        }
        if (deadline && scheduler_get_ticks() >= deadline) {
            return -1;
        }
        fabric_netif_poll_all();
        scheduler_yield();
    }
}

int net_socket_recv(net_socket_t* sock, void* buf, size_t len, uint64_t timeout_ms)
{
    return net_socket_recvfrom(sock, buf, len, NULL, timeout_ms);
}

int net_socket_tcp_recv(net_socket_t* sock, void* buf, size_t len,
                        uint64_t timeout_ms)
{
    if (!sock || !buf || len == 0) {
        return -1;
    }
    if (sock->type != SOCK_STREAM || !sock->tcp_conn) {
        return -1;
    }

    uint64_t deadline = net_deadline_from_ms(timeout_ms);

    for (;;) {
        int got = tcp_conn_rx_take(sock->tcp_conn, buf, len);
        if (got >= 0) {
            return got;   /* >0 = data, 0 = peer EOF */
        }

        if (deadline && scheduler_get_ticks() >= deadline) {
            return -1;
        }
        fabric_netif_poll_all();
        scheduler_yield();
    }
}

int net_socket_tcp_send(net_socket_t* sock, const void* buf, size_t len)
{
    if (!sock || !buf || len == 0) {
        return -1;
    }
    if (sock->type != SOCK_STREAM || !sock->tcp_conn) {
        return -1;
    }
    if (tcp_conn_state(sock->tcp_conn) != TCP_ST_ESTABLISHED) {
        return -1;
    }

    return tcp_engine_send(sock->tcp_conn, buf, len);
}

/* =========================================================================
 * Teardown
 * ======================================================================= */

void net_socket_close(net_socket_t* sock)
{
    if (!sock) {
        return;
    }

    if (sock->listening) {
        spinlock_lock(&g_tcp_listen_lock);
        if (g_tcp_listen_table[sock->bound_port] == sock) {
            g_tcp_listen_table[sock->bound_port] = NULL;
        }
        spinlock_unlock(&g_tcp_listen_lock);

        /* Free connections still waiting to be accepted */
        spinlock_lock(&sock->accept_lock);
        tcp_conn_t* c = sock->accept_head;
        while (c) {
            tcp_conn_t* next = c->next;
            kfree(c);
            c = next;
        }
        sock->accept_head = NULL;
        sock->accept_tail = NULL;
        sock->accept_count = 0;
        spinlock_unlock(&sock->accept_lock);
    }

    if (sock->tcp_conn) {
        tcp_conn_t* conn = sock->tcp_conn;

        if (tcp_engine_send_fin(conn) == 0) {
            /* Best-effort wait for graceful teardown (2 s max) */
            uint64_t dl = net_deadline_from_ms(2000u);
            for (;;) {
                if (tcp_conn_state(conn) == TCP_ST_TIME_WAIT) {
                    break;
                }
                if (scheduler_get_ticks() >= dl) {
                    break;
                }
                fabric_netif_poll_all();
                scheduler_yield();
            }
        }

        tcp_engine_release(conn);
        sock->tcp_conn = NULL;
    }

    if (sock->bound && sock->type == SOCK_DGRAM) {
        spinlock_lock(&udp_port_lock);
        if (udp_port_table[sock->bound_port] == sock) {
            udp_port_table[sock->bound_port] = NULL;
        }
        spinlock_unlock(&udp_port_lock);
    }

    for (;;) {
        uint8_t tmp[1];
        if (udp_queue_pop(&sock->queue, tmp, sizeof(tmp), NULL) < 0) {
            break;
        }
    }

    kfree(sock);
}

/* =========================================================================
 * ICMP ping (waiting lives here; the echo itself is in proto_icmp.c)
 * ======================================================================= */

int net_ping_ipv4(uint32_t dst_host, uint32_t timeout_ms, uint32_t* out_rtt_ms)
{
    if (dst_host == 0) {
        return -1;
    }
    if (socket_layer_ready() != 0) {
        return -1;
    }

    fabric_netif_t* tx_iface = net_select_tx_iface(dst_host);
    if (!tx_iface) {
        return -1;
    }

    uint32_t src_host = 0;
    uint8_t src_mac[BSD_ETHER_ADDR_LEN];
    uint8_t dst_mac[BSD_ETHER_ADDR_LEN];
    if (net_tx_params(tx_iface, dst_host, &src_host, src_mac, dst_mac) != 0) {
        return -1;
    }

    uint16_t seq = icmp_proto_send_echo(tx_iface, src_host, dst_host,
                                        src_mac, dst_mac);
    if (seq == 0) {
        return -1;
    }

    uint64_t deadline = net_deadline_from_ms(timeout_ms);
    if (deadline == 0) {
        deadline = scheduler_get_ticks() + 1;
    }

    for (;;) {
        uint32_t rtt = 0;
        if (icmp_proto_poll_reply(seq, &rtt)) {
            if (out_rtt_ms) {
                *out_rtt_ms = rtt;
            }
            return 0;
        }

        if (scheduler_get_ticks() >= deadline) {
            icmp_proto_cancel(seq);
            return -1;
        }

        fabric_netif_poll_all();
        scheduler_yield();
    }
}
