#include "bsd_netisr.h"
#include "../fabric/spin.h"
#include <stddef.h>

typedef struct bsd_netisr_slot {
    bsd_mbuf_t* q[BSD_NETISR_QDEPTH];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    bsd_netisr_handler_t handler;
    spinlock_t lock;
} bsd_netisr_slot_t;

static bsd_netisr_slot_t g_isr[BSD_NETISR_MAX];
static int g_netisr_inited = 0;

int bsd_netisr_init(void)
{
    if (g_netisr_inited) {
        return 0;
    }

    for (uint32_t i = 0; i < BSD_NETISR_MAX; i++) {
        g_isr[i].head = 0;
        g_isr[i].tail = 0;
        g_isr[i].count = 0;
        g_isr[i].handler.nh_name = NULL;
        g_isr[i].handler.nh_handler = NULL;
        g_isr[i].handler.nh_proto = i;
        g_isr[i].handler.nh_qlimit = BSD_NETISR_QDEPTH;
        spinlock_init(&g_isr[i].lock);
    }

    g_netisr_inited = 1;
    return 0;
}

int bsd_netisr_register(const bsd_netisr_handler_t* nh)
{
    if (!nh || !nh->nh_handler || nh->nh_proto >= BSD_NETISR_MAX) {
        return -1;
    }

    (void)bsd_netisr_init();

    bsd_netisr_slot_t* slot = &g_isr[nh->nh_proto];
    spinlock_lock(&slot->lock);
    slot->handler = *nh;
    if (slot->handler.nh_qlimit == 0 || slot->handler.nh_qlimit > BSD_NETISR_QDEPTH) {
        slot->handler.nh_qlimit = BSD_NETISR_QDEPTH;
    }
    spinlock_unlock(&slot->lock);

    return 0;
}

static int bsd_netisr_enqueue_locked(bsd_netisr_slot_t* slot, bsd_mbuf_t* m)
{
    if (slot->count >= slot->handler.nh_qlimit) {
        return -1;
    }

    slot->q[slot->tail] = m;
    slot->tail = (slot->tail + 1u) % BSD_NETISR_QDEPTH;
    slot->count++;
    return 0;
}

static bsd_mbuf_t* bsd_netisr_dequeue_locked(bsd_netisr_slot_t* slot)
{
    if (slot->count == 0) {
        return NULL;
    }

    bsd_mbuf_t* item = slot->q[slot->head];
    slot->head = (slot->head + 1u) % BSD_NETISR_QDEPTH;
    slot->count--;
    return item;
}

int bsd_netisr_queue(uint32_t proto, bsd_mbuf_t* m)
{
    if (!m || proto >= BSD_NETISR_MAX) {
        return -1;
    }

    (void)bsd_netisr_init();

    bsd_netisr_slot_t* slot = &g_isr[proto];
    int rc = -1;

    spinlock_lock(&slot->lock);
    if (slot->handler.nh_handler) {
        rc = bsd_netisr_enqueue_locked(slot, m);
    }
    spinlock_unlock(&slot->lock);

    return rc;
}

int bsd_netisr_dispatch(uint32_t proto, bsd_mbuf_t* m)
{
    if (!m || proto >= BSD_NETISR_MAX) {
        return -1;
    }

    (void)bsd_netisr_init();

    bsd_netisr_slot_t* slot = &g_isr[proto];
    bsd_netisr_handler_fn_t handler = NULL;

    spinlock_lock(&slot->lock);
    if (!slot->handler.nh_handler || bsd_netisr_enqueue_locked(slot, m) != 0) {
        spinlock_unlock(&slot->lock);
        return -1;
    }
    handler = slot->handler.nh_handler;
    spinlock_unlock(&slot->lock);

    /* MVP: drain synchronously; queue semantics are kept for compatibility. */
    for (;;) {
        bsd_mbuf_t* item = NULL;

        spinlock_lock(&slot->lock);
        item = bsd_netisr_dequeue_locked(slot);
        spinlock_unlock(&slot->lock);

        if (!item) {
            break;
        }
        handler(item);
    }

    return 0;
}
