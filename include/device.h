#ifndef _RODNIX_DEVICE_H
#define _RODNIX_DEVICE_H

#include "types.h"

/* Типы устройств */
typedef enum {
    DEVICE_UNKNOWN = 0,
    DEVICE_DISK,        /* Дисковые устройства (ATA, IDE) */
    DEVICE_CHAR,        /* Символьные устройства (консоль, терминал) */
    DEVICE_BLOCK,       /* Блочные устройства */
    DEVICE_NETWORK,     /* Сетевые устройства */
    DEVICE_MAX
} device_type_t;

/* Состояние устройства */
typedef enum {
    DEVICE_STATE_UNINITIALIZED = 0,
    DEVICE_STATE_INITIALIZED,
    DEVICE_STATE_READY,
    DEVICE_STATE_ERROR,
    DEVICE_STATE_OFFLINE
} device_state_t;

/* Структура устройства */
struct device {
    char name[32];              /* Имя устройства (например, "ata0", "console") */
    device_type_t type;         /* Тип устройства */
    device_state_t state;       /* Состояние устройства */
    uint32_t id;                /* Уникальный ID устройства */
    
    /* Методы устройства */
    int (*init)(struct device* dev);
    int (*read)(struct device* dev, void* buffer, uint32_t offset, uint32_t size);
    int (*write)(struct device* dev, const void* buffer, uint32_t offset, uint32_t size);
    int (*ioctl)(struct device* dev, uint32_t cmd, void* arg);
    void (*close)(struct device* dev);
    
    /* Данные устройства (специфичные для каждого типа) */
    void* private_data;
    
    /* Следующее устройство в списке */
    struct device* next;
};

/* Функции управления устройствами */
int device_register(struct device* dev);
struct device* device_find(const char* name);
struct device* device_find_by_type(device_type_t type);
void device_init_all(void);
void device_list_all(void);

/* Макросы для работы с устройствами */
#define DEVICE_INIT(dev) ((dev) && (dev)->init ? (dev)->init(dev) : -1)
#define DEVICE_READ(dev, buf, off, sz) ((dev) && (dev)->read ? (dev)->read(dev, buf, off, sz) : -1)
#define DEVICE_WRITE(dev, buf, off, sz) ((dev) && (dev)->write ? (dev)->write(dev, buf, off, sz) : -1)

#endif

