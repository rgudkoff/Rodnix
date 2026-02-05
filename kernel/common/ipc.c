/**
 * @file ipc.c
 * @brief Inter-Process Communication implementation
 */

#include "ipc.h"
#include "../core/interrupts.h"
#include "scheduler.h"
#include "../fabric/spin.h"
#include "heap.h"
#include "../../include/common.h"
#include <stddef.h>
#include <stdbool.h>

static bool ipc_initialized = false;
static uint64_t next_port_id = 1;
static uint64_t next_set_id = 1;
static port_t* bootstrap_port = NULL;

/* Simple port table (fixed size for now) */
#define IPC_MAX_PORTS 1024
static port_t* port_table[IPC_MAX_PORTS];

typedef struct ipc_msg_node {
    ipc_message_t msg;
    struct ipc_msg_node* next;
} ipc_msg_node_t;

typedef struct ipc_queue {
    ipc_msg_node_t* head;
    ipc_msg_node_t* tail;
    uint32_t count;
    spinlock_t lock;
} ipc_queue_t;

static ipc_queue_t* ipc_queue_create(void)
{
    ipc_queue_t* q = (ipc_queue_t*)kmalloc(sizeof(ipc_queue_t));
    if (!q) {
        return NULL;
    }
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    spinlock_init(&q->lock);
    return q;
}

static void ipc_queue_destroy(ipc_queue_t* q)
{
    if (!q) {
        return;
    }
    spinlock_lock(&q->lock);
    ipc_msg_node_t* node = q->head;
    while (node) {
        ipc_msg_node_t* next = node->next;
        if (node->msg.data) {
            kfree(node->msg.data);
        }
        kfree(node);
        node = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    spinlock_unlock(&q->lock);
    kfree(q);
}

static int ipc_queue_push(ipc_queue_t* q, const ipc_message_t* message)
{
    if (!q || !message) {
        return -1;
    }
    if (message->msg_size > IPC_MSG_MAX_SIZE) {
        return -1;
    }
    if (message->port_count > IPC_MAX_PORTS_PER_MSG) {
        return -1;
    }
    if (message->msg_size > 0 && !message->data) {
        return -1;
    }
    if (message->msg_size > 0 && !message->data) {
        return -1;
    }
    ipc_msg_node_t* node = (ipc_msg_node_t*)kmalloc(sizeof(ipc_msg_node_t));
    if (!node) {
        return -1;
    }
    node->next = NULL;
    memset(&node->msg, 0, sizeof(node->msg));
    node->msg.msg_id = message->msg_id;
    node->msg.msg_size = message->msg_size;
    node->msg.port_count = message->port_count;
    node->msg.reply_port = message->reply_port;
    if (message->port_count > 0) {
        memcpy(node->msg.ports, message->ports, message->port_count * sizeof(uint64_t));
    }
    if (message->msg_size > 0) {
        node->msg.data = (uint8_t*)kmalloc(message->msg_size);
        if (!node->msg.data) {
            kfree(node);
            return -1;
        }
        memcpy(node->msg.data, message->data, message->msg_size);
    }

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

static int ipc_queue_pop(ipc_queue_t* q, ipc_message_t* out)
{
    if (!q || !out) {
        return -1;
    }
    spinlock_lock(&q->lock);
    ipc_msg_node_t* node = q->head;
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

    *out = node->msg;
    node->msg.data = NULL;
    kfree(node);
    return 0;
}

static inline int port_table_index(uint64_t port_id)
{
    if (port_id == 0 || port_id > IPC_MAX_PORTS) {
        return -1;
    }
    return (int)(port_id - 1);
}

int ipc_init(void)
{
    if (ipc_initialized) {
        return 0;
    }
    
    next_port_id = 1;
    next_set_id = 1;

    for (int i = 0; i < IPC_MAX_PORTS; i++) {
        port_table[i] = NULL;
    }

    ipc_initialized = true;
    /* Reserve bootstrap port (placeholder, no protocol yet) */
    bootstrap_port = port_allocate(PORT_TYPE_CONTROL);
    return 0;
}

port_t* port_allocate(port_type_t type)
{
    if (!ipc_initialized) {
        ipc_init();
    }
    
    port_t* port = (port_t*)kmalloc(sizeof(port_t));
    
    if (!port) {
        return NULL;
    }

    if (next_port_id > IPC_MAX_PORTS) {
        kfree(port);
        return NULL;
    }

    port->port_id = next_port_id++;
    port->type = type;
    port->rights = PORT_RIGHT_RECEIVE | PORT_RIGHT_SEND;
    port->owner = task_get_current();
    port->owner_thread = thread_get_current();
    port->ref_count = 1;
    port->queue = ipc_queue_create();
    if (!port->queue) {
        kfree(port);
        return NULL;
    }
    port->active = true;

    int idx = port_table_index(port->port_id);
    if (idx >= 0) {
        port_table[idx] = port;
    }
    
    return port;
}

void port_deallocate(port_t* port)
{
    if (!port) {
        return;
    }
    
    port->ref_count--;
    
    if (port->ref_count == 0) {
        port->active = false;
        ipc_queue_destroy((ipc_queue_t*)port->queue);
        port->queue = NULL;
        int idx = port_table_index(port->port_id);
        if (idx >= 0 && port_table[idx] == port) {
            port_table[idx] = NULL;
        }
        kfree(port);
    }
}

port_t* ipc_get_bootstrap_port(void)
{
    return bootstrap_port;
}

void ipc_message_free(ipc_message_t* message)
{
    if (!message) {
        return;
    }
    if (message->data) {
        kfree(message->data);
        message->data = NULL;
    }
    message->msg_size = 0;
    message->port_count = 0;
}

port_t* port_lookup(uint64_t port_id)
{
    int idx = port_table_index(port_id);
    if (idx < 0) {
        return NULL;
    }
    return port_table[idx];
}

int port_insert_send_right(task_t* task, port_t* port)
{
    if (!task || !port) {
        return -1;
    }
    
    /* TODO: Task port namespaces not implemented */
    (void)task;
    port->rights |= PORT_RIGHT_SEND;
    port->ref_count++;
    return 0;
}

int port_insert_receive_right(task_t* task, port_t* port)
{
    if (!task || !port) {
        return -1;
    }
    
    /* TODO: Task port namespaces not implemented */
    port->owner = task;
    port->rights |= PORT_RIGHT_RECEIVE;
    port->ref_count++;
    return 0;
}

int ipc_send(port_t* port, ipc_message_t* message, uint64_t timeout)
{
    if (!port || !message) {
        return -1;
    }
    
    if (!port->active) {
        return -1;
    }

    if ((port->rights & PORT_RIGHT_SEND) == 0) {
        return -1;
    }
    
    (void)timeout;
    if (message->msg_size > IPC_MSG_MAX_SIZE) {
        return -1;
    }
    if (message->port_count > IPC_MAX_PORTS_PER_MSG) {
        return -1;
    }
    if (!port->queue) {
        return -1;
    }
    uint32_t bumped = 0;
    for (uint32_t i = 0; i < message->port_count; i++) {
        port_t* p = port_lookup(message->ports[i]);
        if (!p || !p->active) {
            return -1;
        }
        p->ref_count++;
        bumped++;
    }
    if (ipc_queue_push((ipc_queue_t*)port->queue, message) != 0) {
        for (uint32_t i = 0; i < bumped; i++) {
            port_t* p = port_lookup(message->ports[i]);
            if (p && p->ref_count > 0) {
                p->ref_count--;
            }
        }
        return -1;
    }
    
    return 0;
}

static uint64_t ipc_get_deadline_ticks(uint64_t timeout_ms)
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

int ipc_receive(port_t* port, ipc_message_t* message, uint64_t timeout)
{
    if (!port || !message) {
        return -1;
    }
    
    if (!port->active) {
        return -1;
    }

    if (port->owner) {
        task_t* current = task_get_current();
        if (current != port->owner) {
            return -1;
        }
    }
    
    thread_t* receiver = thread_get_current();
    if (port->owner_thread && receiver) {
        scheduler_inherit_priority(port->owner_thread, receiver);
    }

    uint64_t deadline = ipc_get_deadline_ticks(timeout);
    if (!port->queue) {
        if (port->owner_thread) {
            scheduler_clear_inherit(port->owner_thread);
        }
        return -1;
    }
    for (;;) {
        if (ipc_queue_pop((ipc_queue_t*)port->queue, message) == 0) {
            if (port->owner_thread) {
                scheduler_clear_inherit(port->owner_thread);
            }
            return 0;
        }
        if (deadline && scheduler_get_ticks() >= deadline) {
            if (port->owner_thread) {
                scheduler_clear_inherit(port->owner_thread);
            }
            return -1;
        }
        scheduler_yield();
    }
    
}

int ipc_send_receive(port_t* port, ipc_message_t* send_msg, 
                     ipc_message_t* reply_msg, uint64_t timeout)
{
    if (!port || !send_msg || !reply_msg) {
        return -1;
    }
    
    if (!send_msg->reply_port) {
        return -1;
    }

    thread_t* sender = thread_get_current();
    if (port->owner_thread && sender) {
        scheduler_inherit_priority(port->owner_thread, sender);
    }

    int ret = ipc_send(port, send_msg, timeout);
    if (ret != 0) {
        if (port->owner_thread) {
            scheduler_clear_inherit(port->owner_thread);
        }
        return ret;
    }
    
    ret = ipc_receive(send_msg->reply_port, reply_msg, timeout);
    if (port->owner_thread) {
        scheduler_clear_inherit(port->owner_thread);
    }
    
    return ret;
}

port_set_t* port_set_create(void)
{
    if (!ipc_initialized) {
        ipc_init();
    }
    
    port_set_t* set = (port_set_t*)kmalloc(sizeof(port_set_t));
    
    if (!set) {
        return NULL;
    }
    
    set->set_id = next_set_id++;
    set->owner = task_get_current();
    set->ports = NULL;
    set->port_count = 0;
    set->capacity = 0;
    
    return set;
}

void port_set_destroy(port_set_t* set)
{
    if (!set) {
        return;
    }
    
    if (set->ports) {
        kfree(set->ports);
        set->ports = NULL;
    }
    kfree(set);
}

int port_set_add(port_set_t* set, port_t* port)
{
    if (!set || !port) {
        return -1;
    }
    
    for (uint32_t i = 0; i < set->port_count; i++) {
        if (set->ports[i] == port) {
            return 0;
        }
    }
    if (set->port_count == set->capacity) {
        uint32_t new_cap = (set->capacity == 0) ? 4 : (set->capacity * 2);
        port_t** new_ports = (port_t**)krealloc(set->ports, new_cap * sizeof(port_t*));
        if (!new_ports) {
            return -1;
        }
        set->ports = new_ports;
        set->capacity = new_cap;
    }
    set->ports[set->port_count++] = port;
    
    return 0;
}

int port_set_remove(port_set_t* set, port_t* port)
{
    if (!set || !port) {
        return -1;
    }
    
    for (uint32_t i = 0; i < set->port_count; i++) {
        if (set->ports[i] == port) {
            for (uint32_t j = i + 1; j < set->port_count; j++) {
                set->ports[j - 1] = set->ports[j];
            }
            set->port_count--;
            return 0;
        }
    }

    return -1;
}

int port_set_receive(port_set_t* set, ipc_message_t* message, uint64_t timeout)
{
    if (!set || !message) {
        return -1;
    }
    
    uint64_t deadline = ipc_get_deadline_ticks(timeout);
    for (;;) {
        for (uint32_t i = 0; i < set->port_count; i++) {
            port_t* port = set->ports[i];
            if (!port || !port->active || !port->queue) {
                continue;
            }
            thread_t* receiver = thread_get_current();
            if (port->owner_thread && receiver) {
                scheduler_inherit_priority(port->owner_thread, receiver);
            }
            if (ipc_queue_pop((ipc_queue_t*)port->queue, message) == 0) {
                if (port->owner_thread) {
                    scheduler_clear_inherit(port->owner_thread);
                }
                return 0;
            }
            if (port->owner_thread) {
                scheduler_clear_inherit(port->owner_thread);
            }
        }
        if (deadline && scheduler_get_ticks() >= deadline) {
            return -1;
        }
        scheduler_yield();
    }
}
