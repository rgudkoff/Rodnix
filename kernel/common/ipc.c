/**
 * @file ipc.c
 * @brief Inter-Process Communication implementation
 */

#include "ipc.h"
#include "../core/interrupts.h"
#include "../../include/common.h"
#include <stddef.h>
#include <stdbool.h>

static bool ipc_initialized = false;
static uint64_t next_port_id = 1;
static uint64_t next_set_id = 1;

/* TODO: Implement port tables, message queues, etc. */

int ipc_init(void)
{
    if (ipc_initialized) {
        return 0;
    }
    
    next_port_id = 1;
    next_set_id = 1;
    
    /* TODO: Initialize port tables */
    /* TODO: Initialize message queues */
    
    ipc_initialized = true;
    return 0;
}

port_t* port_allocate(port_type_t type)
{
    if (!ipc_initialized) {
        ipc_init();
    }
    
    port_t* port = NULL;  /* TODO: Allocate from pool */
    
    if (!port) {
        return NULL;
    }
    
    port->port_id = next_port_id++;
    port->type = type;
    port->rights = 0;
    port->owner = task_get_current();
    port->ref_count = 1;
    port->queue = NULL;  /* TODO: Initialize message queue */
    port->active = true;
    
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
        /* TODO: Free message queue */
        /* TODO: Return to pool */
    }
}

port_t* port_lookup(uint64_t port_id)
{
    /* TODO: Lookup port in port table */
    (void)port_id;
    return NULL;
}

int port_insert_send_right(task_t* task, port_t* port)
{
    if (!task || !port) {
        return -1;
    }
    
    /* TODO: Insert send right into task's port namespace */
    
    port->ref_count++;
    return 0;
}

int port_insert_receive_right(task_t* task, port_t* port)
{
    if (!task || !port) {
        return -1;
    }
    
    /* TODO: Insert receive right into task's port namespace */
    
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
    
    /* TODO: Add message to port's queue */
    /* TODO: Wake up any threads waiting on this port */
    
    return 0;
}

int ipc_receive(port_t* port, ipc_message_t* message, uint64_t timeout)
{
    if (!port || !message) {
        return -1;
    }
    
    if (!port->active) {
        return -1;
    }
    
    /* TODO: Wait for message in port's queue */
    /* TODO: Copy message to buffer */
    /* TODO: Handle timeout */
    
    return 0;
}

int ipc_send_receive(port_t* port, ipc_message_t* send_msg, 
                     ipc_message_t* reply_msg, uint64_t timeout)
{
    if (!port || !send_msg || !reply_msg) {
        return -1;
    }
    
    /* TODO: Send message */
    int ret = ipc_send(port, send_msg, timeout);
    if (ret != 0) {
        return ret;
    }
    
    /* TODO: Wait for reply on reply port */
    /* TODO: Receive reply message */
    
    return 0;
}

port_set_t* port_set_create(void)
{
    if (!ipc_initialized) {
        ipc_init();
    }
    
    port_set_t* set = NULL;  /* TODO: Allocate from pool */
    
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
    
    /* TODO: Remove all ports from set */
    /* TODO: Free ports array */
    /* TODO: Return to pool */
}

int port_set_add(port_set_t* set, port_t* port)
{
    if (!set || !port) {
        return -1;
    }
    
    /* TODO: Check if port is already in set */
    /* TODO: Resize array if needed */
    /* TODO: Add port to array */
    
    return 0;
}

int port_set_remove(port_set_t* set, port_t* port)
{
    if (!set || !port) {
        return -1;
    }
    
    /* TODO: Find port in array */
    /* TODO: Remove from array */
    /* TODO: Compact array */
    
    return 0;
}

int port_set_receive(port_set_t* set, ipc_message_t* message, uint64_t timeout)
{
    if (!set || !message) {
        return -1;
    }
    
    /* TODO: Wait for message on any port in set */
    /* TODO: Receive message from first available port */
    /* TODO: Handle timeout */
    
    return 0;
}

