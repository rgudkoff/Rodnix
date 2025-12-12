/**
 * @file device.h
 * @brief Device management interface
 * 
 * Provides device registration, discovery, and management functionality.
 */

#ifndef _RODNIX_COMMON_DEVICE_H
#define _RODNIX_COMMON_DEVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Device types
 * ============================================================================ */

typedef enum {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_CHAR,        /* Character device */
    DEVICE_TYPE_BLOCK,       /* Block device */
    DEVICE_TYPE_NETWORK,     /* Network device */
    DEVICE_TYPE_BUS,         /* Bus device */
    DEVICE_TYPE_INPUT,       /* Input device */
    DEVICE_TYPE_DISPLAY,     /* Display device */
    DEVICE_TYPE_AUDIO,       /* Audio device */
} device_type_t;

/* ============================================================================
 * Device states
 * ============================================================================ */

typedef enum {
    DEVICE_STATE_UNINITIALIZED = 0,
    DEVICE_STATE_INITIALIZING,
    DEVICE_STATE_READY,
    DEVICE_STATE_ERROR,
    DEVICE_STATE_OFFLINE,
} device_state_t;

/* ============================================================================
 * Device operations
 * ============================================================================ */

typedef struct device_ops {
    int (*init)(void* dev);
    int (*deinit)(void* dev);
    int (*read)(void* dev, void* buffer, size_t size, uint64_t offset);
    int (*write)(void* dev, const void* buffer, size_t size, uint64_t offset);
    int (*ioctl)(void* dev, uint32_t cmd, void* arg);
    int (*probe)(void* dev);
    int (*remove)(void* dev);
} device_ops_t;

/* ============================================================================
 * Device
 * ============================================================================ */

typedef struct device {
    uint64_t device_id;         /* Unique device identifier */
    const char* name;            /* Device name */
    device_type_t type;         /* Device type */
    device_state_t state;       /* Device state */
    void* private_data;         /* Device-specific data */
    device_ops_t* ops;          /* Device operations */
    struct device* parent;      /* Parent device */
    struct device** children;    /* Child devices */
    uint32_t child_count;       /* Number of children */
    uint32_t ref_count;         /* Reference count */
} device_t;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize device management subsystem
 * @return 0 on success, negative value on error
 */
int device_manager_init(void);

/* ============================================================================
 * Device registration
 * ============================================================================ */

/**
 * Register a device
 * @param device Device to register
 * @return 0 on success, negative value on error
 */
int device_register(device_t* device);

/**
 * Unregister a device
 * @param device Device to unregister
 */
void device_unregister(device_t* device);

/**
 * Find device by ID
 * @param device_id Device identifier
 * @return Pointer to device or NULL if not found
 */
device_t* device_find_by_id(uint64_t device_id);

/**
 * Find device by name
 * @param name Device name
 * @return Pointer to device or NULL if not found
 */
device_t* device_find_by_name(const char* name);

/* ============================================================================
 * Device operations
 * ============================================================================ */

/**
 * Initialize a device
 * @param device Device to initialize
 * @return 0 on success, negative value on error
 */
int device_init(device_t* device);

/**
 * Deinitialize a device
 * @param device Device to deinitialize
 */
void device_deinit(device_t* device);

/**
 * Read from device
 * @param device Device to read from
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @param offset Offset to read from
 * @return Number of bytes read, negative value on error
 */
int device_read(device_t* device, void* buffer, size_t size, uint64_t offset);

/**
 * Write to device
 * @param device Device to write to
 * @param buffer Buffer to write from
 * @param size Number of bytes to write
 * @param offset Offset to write to
 * @return Number of bytes written, negative value on error
 */
int device_write(device_t* device, const void* buffer, size_t size, uint64_t offset);

/**
 * I/O control
 * @param device Device
 * @param cmd Control command
 * @param arg Command argument
 * @return 0 on success, negative value on error
 */
int device_ioctl(device_t* device, uint32_t cmd, void* arg);

/* ============================================================================
 * Device tree
 * ============================================================================ */

/**
 * Add child device
 * @param parent Parent device
 * @param child Child device
 * @return 0 on success, negative value on error
 */
int device_add_child(device_t* parent, device_t* child);

/**
 * Remove child device
 * @param parent Parent device
 * @param child Child device
 * @return 0 on success, negative value on error
 */
int device_remove_child(device_t* parent, device_t* child);

/**
 * Get device count
 * @return Number of registered devices
 */
uint32_t device_get_count(void);

#endif /* _RODNIX_COMMON_DEVICE_H */

