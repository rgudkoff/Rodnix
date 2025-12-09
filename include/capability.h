#ifndef _RODNIX_CAPABILITY_H
#define _RODNIX_CAPABILITY_H

#include "types.h"

/* Типы capabilities */
typedef enum {
    CAP_DEVICE_READ = 1,
    CAP_DEVICE_WRITE = 2,
    CAP_DEVICE_IOCTL = 4,
    CAP_DRIVER_LOAD = 8,
    CAP_DRIVER_UNLOAD = 16,
    CAP_MEMORY_ALLOC = 32,
    CAP_IPC_SEND = 64,
    CAP_IPC_RECV = 128,
    CAP_PCI_ACCESS = 256,
    CAP_ACPI_ACCESS = 512
} capability_type_t;

/* Структура capability */
typedef struct {
    uint32_t pid;              /* PID процесса, которому принадлежит capability */
    capability_type_t type;    /* Тип capability */
    uint32_t resource_id;      /* ID ресурса (например, device ID) */
    uint64_t flags;            /* Дополнительные флаги */
} capability_t;

/* Инициализация системы capabilities */
int capability_init(void);

/* Проверка capability */
int capability_check(uint32_t pid, capability_type_t type, uint32_t resource_id);

/* Выдача capability */
int capability_grant(uint32_t pid, capability_type_t type, uint32_t resource_id, uint64_t flags);

/* Отзыв capability */
int capability_revoke(uint32_t pid, capability_type_t type, uint32_t resource_id);

/* Список capabilities процесса */
int capability_list(uint32_t pid, capability_t* caps, uint32_t max_caps);

#endif

