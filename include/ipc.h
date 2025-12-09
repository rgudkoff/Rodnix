#ifndef _RODNIX_IPC_H
#define _RODNIX_IPC_H

#include "types.h"

/* Максимальный размер сообщения IPC */
#define IPC_MSG_SIZE 256

/* Типы сообщений IPC */
typedef enum {
    IPC_MSG_DEVICE_REGISTER = 1,
    IPC_MSG_DEVICE_UNREGISTER,
    IPC_MSG_DEVICE_QUERY,
    IPC_MSG_DRIVER_LOAD,
    IPC_MSG_DRIVER_UNLOAD,
    IPC_MSG_CAPABILITY_REQUEST,
    IPC_MSG_CAPABILITY_GRANT
} ipc_msg_type_t;

/* Структура сообщения IPC */
typedef struct {
    uint32_t from_pid;      /* PID отправителя */
    uint32_t to_pid;        /* PID получателя */
    ipc_msg_type_t type;    /* Тип сообщения */
    uint32_t data_len;      /* Длина данных */
    uint8_t data[IPC_MSG_SIZE];  /* Данные сообщения */
} ipc_message_t;

/* Инициализация системы IPC */
int ipc_init(void);

/* Отправка сообщения */
int ipc_send(uint32_t to_pid, ipc_message_t* msg);

/* Получение сообщения */
int ipc_recv(uint32_t from_pid, ipc_message_t* msg, uint32_t timeout_ms);

/* Проверка наличия сообщений */
int ipc_has_message(uint32_t pid);

#endif

