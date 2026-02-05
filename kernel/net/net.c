#include "net.h"
#include "../fabric/spin.h"
#include "../common/heap.h"
#include "../../include/console.h"

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

int net_loopback_recv(net_packet_t* out)
{
    if (!loopback_queue || !out) {
        return -1;
    }
    return net_queue_pop(loopback_queue, out);
}
