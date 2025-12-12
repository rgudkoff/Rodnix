/**
 * @file ipc.h
 * @brief Inter-Process Communication interface
 * 
 * Provides mechanisms for communication between tasks and threads.
 */

#ifndef _RODNIX_COMMON_IPC_H
#define _RODNIX_COMMON_IPC_H

#include "../core/task.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Port types
 * ============================================================================ */

typedef enum {
    PORT_TYPE_NORMAL = 0,     /* Normal message port */
    PORT_TYPE_NOTIFICATION,   /* Notification port */
    PORT_TYPE_CONTROL,        /* Control port */
} port_type_t;

/* ============================================================================
 * Port rights
 * ============================================================================ */

#define PORT_RIGHT_SEND      (1UL << 0)
#define PORT_RIGHT_RECEIVE   (1UL << 1)
#define PORT_RIGHT_SEND_ONCE (1UL << 2)
#define PORT_RIGHT_PORT_SET  (1UL << 3)
#define PORT_RIGHT_DEAD_NAME (1UL << 4)

/* ============================================================================
 * Port
 * ============================================================================ */

typedef struct port {
    uint64_t port_id;         /* Unique port identifier */
    port_type_t type;         /* Port type */
    uint64_t rights;          /* Port rights */
    task_t* owner;            /* Owning task */
    uint32_t ref_count;       /* Reference count */
    void* queue;              /* Message queue */
    bool active;              /* Is port active */
} port_t;

/* ============================================================================
 * Message
 * ============================================================================ */

#define IPC_MSG_MAX_SIZE 4096

typedef struct {
    uint64_t msg_id;          /* Message identifier */
    uint32_t msg_size;        /* Message size */
    uint8_t data[IPC_MSG_MAX_SIZE];  /* Message data */
    port_t* reply_port;       /* Reply port (optional) */
} ipc_message_t;

/* ============================================================================
 * Port set
 * ============================================================================ */

typedef struct port_set {
    uint64_t set_id;          /* Unique set identifier */
    task_t* owner;            /* Owning task */
    port_t** ports;           /* Array of ports */
    uint32_t port_count;      /* Number of ports */
    uint32_t capacity;        /* Capacity of ports array */
} port_set_t;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize IPC subsystem
 * @return 0 on success, negative value on error
 */
int ipc_init(void);

/* ============================================================================
 * Port management
 * ============================================================================ */

/**
 * Allocate a new port
 * @param type Port type
 * @return Pointer to port or NULL on error
 */
port_t* port_allocate(port_type_t type);

/**
 * Deallocate a port
 * @param port Port to deallocate
 */
void port_deallocate(port_t* port);

/**
 * Get port by ID
 * @param port_id Port identifier
 * @return Pointer to port or NULL if not found
 */
port_t* port_lookup(uint64_t port_id);

/**
 * Insert send right into task
 * @param task Target task
 * @param port Port to insert
 * @return 0 on success, negative value on error
 */
int port_insert_send_right(task_t* task, port_t* port);

/**
 * Insert receive right into task
 * @param task Target task
 * @param port Port to insert
 * @return 0 on success, negative value on error
 */
int port_insert_receive_right(task_t* task, port_t* port);

/* ============================================================================
 * Message passing
 * ============================================================================ */

/**
 * Send a message to a port
 * @param port Destination port
 * @param message Message to send
 * @param timeout Timeout in milliseconds (0 = infinite)
 * @return 0 on success, negative value on error
 */
int ipc_send(port_t* port, ipc_message_t* message, uint64_t timeout);

/**
 * Receive a message from a port
 * @param port Source port
 * @param message Buffer for received message
 * @param timeout Timeout in milliseconds (0 = infinite)
 * @return 0 on success, negative value on error
 */
int ipc_receive(port_t* port, ipc_message_t* message, uint64_t timeout);

/**
 * Send a message and wait for reply
 * @param port Destination port
 * @param send_msg Message to send
 * @param reply_msg Buffer for reply message
 * @param timeout Timeout in milliseconds (0 = infinite)
 * @return 0 on success, negative value on error
 */
int ipc_send_receive(port_t* port, ipc_message_t* send_msg, 
                     ipc_message_t* reply_msg, uint64_t timeout);

/* ============================================================================
 * Port sets
 * ============================================================================ */

/**
 * Create a port set
 * @return Pointer to port set or NULL on error
 */
port_set_t* port_set_create(void);

/**
 * Destroy a port set
 * @param set Port set to destroy
 */
void port_set_destroy(port_set_t* set);

/**
 * Add port to set
 * @param set Port set
 * @param port Port to add
 * @return 0 on success, negative value on error
 */
int port_set_add(port_set_t* set, port_t* port);

/**
 * Remove port from set
 * @param set Port set
 * @param port Port to remove
 * @return 0 on success, negative value on error
 */
int port_set_remove(port_set_t* set, port_t* port);

/**
 * Receive from any port in set
 * @param set Port set
 * @param message Buffer for received message
 * @param timeout Timeout in milliseconds (0 = infinite)
 * @return 0 on success, negative value on error
 */
int port_set_receive(port_set_t* set, ipc_message_t* message, uint64_t timeout);

#endif /* _RODNIX_COMMON_IPC_H */

