#include "net.h"
#include "socket.h"
#include "../fabric/service/net_service.h"
#include "../fabric/spin.h"
#include "../common/heap.h"
#include "../../include/console.h"
#include "../../include/common.h"
#include "../../include/error.h"

typedef struct net_node {
    net_packet_t pkt;
    struct net_node* next;
} net_node_t;

typedef struct net_queue {
    net_node_t* head;
    net_node_t* tail;
    uint32_t count;
    spinlock_t lock;
} net_queue_t;

static net_queue_t* loopback_queue = NULL;
static int net_service_registered = 0;
static fabric_netif_t loopback_iface = {0};

static int net_loopback_tx_cb(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    (void)iface;
    return (net_ingress_frame(frame, len, NULL) == 0) ? RDNX_OK : RDNX_E_GENERIC;
}

static void net_register_loopback_iface_once(void)
{
    if (net_service_registered) {
        return;
    }
    if (fabric_net_service_init() != RDNX_OK) {
        kputs("[NET] Fabric net service init failed\n");
        return;
    }

    static fabric_netif_ops_t loopback_ops = {
        .hdr = RDNX_ABI_INIT(fabric_netif_ops_t),
        .tx = net_loopback_tx_cb
    };

    loopback_iface.hdr = RDNX_ABI_INIT(fabric_netif_t);
    loopback_iface.name = "lo0";
    loopback_iface.mac[0] = 0x02;
    loopback_iface.mac[1] = 0x00;
    loopback_iface.mac[2] = 0x00;
    loopback_iface.mac[3] = 0x00;
    loopback_iface.mac[4] = 0x00;
    loopback_iface.mac[5] = 0x01;
    loopback_iface.mtu = NET_MAX_PACKET;
    loopback_iface.flags = FABRIC_NETIF_F_UP | FABRIC_NETIF_F_LOOPBACK;
    loopback_iface.ipv4_addr = 0x7F000001u;     /* 127.0.0.1 */
    loopback_iface.ipv4_netmask = 0xFF000000u;  /* 255.0.0.0 */
    loopback_iface.ipv4_gateway = 0;
    loopback_iface.ops = &loopback_ops;
    loopback_iface.context = NULL;

    if (fabric_netif_register(&loopback_iface) != RDNX_OK) {
        kputs("[NET] Fabric net iface register failed: lo0\n");
        return;
    }

    net_service_registered = 1;
    kputs("[NET] Fabric net interface ready: lo0\n");
}

static net_queue_t* net_queue_create(void)
{
    net_queue_t* q = (net_queue_t*)kmalloc(sizeof(net_queue_t));
    if (!q) {
        return NULL;
    }
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    spinlock_init(&q->lock);
    return q;
}

static int net_queue_push(net_queue_t* q, const net_packet_t* pkt)
{
    if (!q || !pkt) {
        return -1;
    }
    if (pkt->len > NET_MAX_PACKET) {
        return -1;
    }
    net_node_t* node = (net_node_t*)kmalloc(sizeof(net_node_t));
    if (!node) {
        return -1;
    }
    node->next = NULL;
    node->pkt = *pkt;

    spinlock_lock(&q->lock);
    if (!q->tail) {
        q->head = node;
        q->tail = node;
    } else {
        q->tail->next = node;
        q->tail = node;
    }
    q->count++;
    spinlock_unlock(&q->lock);
    return 0;
}

static int net_queue_pop(net_queue_t* q, net_packet_t* out)
{
    if (!q || !out) {
        return -1;
    }
    spinlock_lock(&q->lock);
    net_node_t* node = q->head;
    if (!node) {
        spinlock_unlock(&q->lock);
        return -1;
    }
    q->head = node->next;
    if (!q->head) {
        q->tail = NULL;
    }
    if (q->count > 0) {
        q->count--;
    }
    spinlock_unlock(&q->lock);

    *out = node->pkt;
    kfree(node);
    return 0;
}

int net_init(void)
{
    if (!loopback_queue) {
        loopback_queue = net_queue_create();
        if (!loopback_queue) {
            kputs("[NET] Loopback init failed\n");
            return -1;
        }
    }
    net_register_loopback_iface_once();
    kputs("[NET] Loopback ready\n");
    return 0;
}

int net_loopback_send(const net_packet_t* pkt)
{
    if (!loopback_queue || !pkt) {
        return -1;
    }
    return net_queue_push(loopback_queue, pkt);
}

int net_loopback_frame_tx(const void* frame, uint32_t len)
{
    if (!frame || len == 0 || len > NET_MAX_PACKET) {
        return RDNX_E_INVALID;
    }
    net_packet_t pkt = {0};
    pkt.len = len;
    memcpy(pkt.data, frame, len);
    if (net_loopback_send(&pkt) != 0) {
        return RDNX_E_GENERIC;
    }
    return RDNX_OK;
}

int net_loopback_recv(net_packet_t* out)
{
    if (!loopback_queue || !out) {
        return -1;
    }
    return net_queue_pop(loopback_queue, out);
}
