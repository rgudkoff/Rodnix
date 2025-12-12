/**
 * @file device.c
 * @brief Device management implementation
 */

#include "device.h"
#include "../../include/common.h"
#include <stddef.h>
#include <stdbool.h>

static bool device_manager_initialized = false;
static uint64_t next_device_id = 1;
static device_t* device_list = NULL;
static uint32_t device_count = 0;

/* TODO: Implement device tree, device tables, etc. */

int device_manager_init(void)
{
    if (device_manager_initialized) {
        return 0;
    }
    
    next_device_id = 1;
    device_list = NULL;
    device_count = 0;
    
    /* TODO: Initialize device tree */
    /* TODO: Initialize device tables */
    
    device_manager_initialized = true;
    return 0;
}

int device_register(device_t* device)
{
    if (!device) {
        return -1;
    }
    
    if (!device_manager_initialized) {
        device_manager_init();
    }
    
    /* TODO: Check if device with same name already exists */
    
    device->device_id = next_device_id++;
    device->ref_count = 1;
    device->state = DEVICE_STATE_UNINITIALIZED;
    
    /* TODO: Add to device list */
    /* TODO: Add to device tree */
    
    device_count++;
    return 0;
}

void device_unregister(device_t* device)
{
    if (!device) {
        return;
    }
    
    device->ref_count--;
    
    if (device->ref_count == 0) {
        device->state = DEVICE_STATE_OFFLINE;
        
        /* TODO: Remove from device list */
        /* TODO: Remove from device tree */
        /* TODO: Deinitialize if needed */
        
        device_count--;
    }
}

device_t* device_find_by_id(uint64_t device_id)
{
    /* TODO: Search device list */
    (void)device_id;
    return NULL;
}

device_t* device_find_by_name(const char* name)
{
    if (!name) {
        return NULL;
    }
    
    /* TODO: Search device list by name */
    (void)name;
    return NULL;
}

int device_init(device_t* device)
{
    if (!device) {
        return -1;
    }
    
    if (!device->ops || !device->ops->init) {
        return -1;
    }
    
    device->state = DEVICE_STATE_INITIALIZING;
    
    int ret = device->ops->init(device);
    
    if (ret == 0) {
        device->state = DEVICE_STATE_READY;
    } else {
        device->state = DEVICE_STATE_ERROR;
    }
    
    return ret;
}

void device_deinit(device_t* device)
{
    if (!device) {
        return;
    }
    
    if (device->ops && device->ops->deinit) {
        device->ops->deinit(device);
    }
    
    device->state = DEVICE_STATE_OFFLINE;
}

int device_read(device_t* device, void* buffer, size_t size, uint64_t offset)
{
    if (!device || !buffer) {
        return -1;
    }
    
    if (device->state != DEVICE_STATE_READY) {
        return -1;
    }
    
    if (!device->ops || !device->ops->read) {
        return -1;
    }
    
    return device->ops->read(device, buffer, size, offset);
}

int device_write(device_t* device, const void* buffer, size_t size, uint64_t offset)
{
    if (!device || !buffer) {
        return -1;
    }
    
    if (device->state != DEVICE_STATE_READY) {
        return -1;
    }
    
    if (!device->ops || !device->ops->write) {
        return -1;
    }
    
    return device->ops->write(device, buffer, size, offset);
}

int device_ioctl(device_t* device, uint32_t cmd, void* arg)
{
    if (!device) {
        return -1;
    }
    
    if (device->state != DEVICE_STATE_READY) {
        return -1;
    }
    
    if (!device->ops || !device->ops->ioctl) {
        return -1;
    }
    
    return device->ops->ioctl(device, cmd, arg);
}

int device_add_child(device_t* parent, device_t* child)
{
    if (!parent || !child) {
        return -1;
    }
    
    child->parent = parent;
    
    /* TODO: Resize children array if needed */
    /* TODO: Add child to array */
    
    parent->child_count++;
    return 0;
}

int device_remove_child(device_t* parent, device_t* child)
{
    if (!parent || !child) {
        return -1;
    }
    
    /* TODO: Find child in array */
    /* TODO: Remove from array */
    /* TODO: Compact array */
    
    child->parent = NULL;
    parent->child_count--;
    return 0;
}

uint32_t device_get_count(void)
{
    return device_count;
}

