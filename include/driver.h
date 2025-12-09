#ifndef _RODNIX_DRIVER_H
#define _RODNIX_DRIVER_H

#include "types.h"
#include "device.h"

/* Интерфейс драйвера устройства */
struct driver {
    /* Имя драйвера */
    const char* name;
    
    /* Версия драйвера */
    uint32_t version;
    
    /* Тип устройства, которое поддерживает драйвер */
    device_type_t device_type;
    
    /* Инициализация драйвера */
    int (*init)(void);
    
    /* Деинициализация драйвера */
    void (*exit)(void);
    
    /* Регистрация устройств, которые нашел драйвер */
    int (*probe)(void);
    
    /* Следующий драйвер в списке */
    struct driver* next;
};

/* Регистрация драйвера в системе */
int driver_register(struct driver* drv);

/* Поиск драйвера по имени */
struct driver* driver_find(const char* name);

/* Поиск драйвера по типу устройства */
struct driver* driver_find_by_type(device_type_t type);

/* Список всех драйверов */
void driver_list_all(void);

/* Инициализация системы драйверов */
int driver_system_init(void);

/* Загрузка всех зарегистрированных драйверов */
int driver_load_all(void);

#endif


