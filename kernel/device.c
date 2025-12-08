#include "../include/device.h"
#include "../include/console.h"
#include "../include/common.h"

/* Список зарегистрированных устройств */
static struct device* device_list = NULL;
static uint32_t device_next_id = 1;

/* Регистрация устройства */
int device_register(struct device* dev)
{
    if (!dev)
        return -1;
    
    /* Проверка, что устройство еще не зарегистрировано */
    struct device* current = device_list;
    while (current)
    {
        if (current == dev)
            return -1; /* Уже зарегистрировано */
        current = current->next;
    }
    
    /* Установка ID, если не установлен */
    if (dev->id == 0)
        dev->id = device_next_id++;
    
    /* Добавление в список */
    dev->next = device_list;
    device_list = dev;
    
    /* Инициализация устройства, если есть метод */
    if (dev->init)
    {
        int result = dev->init(dev);
        if (result == 0)
            dev->state = DEVICE_STATE_READY;
        else
            dev->state = DEVICE_STATE_ERROR;
    }
    else
    {
        dev->state = DEVICE_STATE_INITIALIZED;
    }
    
    return 0;
}

/* Поиск устройства по имени */
struct device* device_find(const char* name)
{
    if (!name)
        return NULL;
    
    struct device* current = device_list;
    while (current)
    {
        int i = 0;
        int match = 1;
        while (name[i] != '\0' && i < 31)
        {
            if (current->name[i] != name[i])
            {
                match = 0;
                break;
            }
            i++;
        }
        if (match && current->name[i] == '\0')
            return current;
        
        current = current->next;
    }
    
    return NULL;
}

/* Поиск устройства по типу (возвращает первое найденное) */
struct device* device_find_by_type(device_type_t type)
{
    struct device* current = device_list;
    while (current)
    {
        if (current->type == type)
            return current;
        current = current->next;
    }
    
    return NULL;
}

/* Инициализация всех устройств */
void device_init_all(void)
{
    struct device* current = device_list;
    while (current)
    {
        if (current->state == DEVICE_STATE_UNINITIALIZED && current->init)
        {
            int result = current->init(current);
            if (result == 0)
                current->state = DEVICE_STATE_READY;
            else
                current->state = DEVICE_STATE_ERROR;
        }
        current = current->next;
    }
}

/* Вывод списка всех устройств */
void device_list_all(void)
{
    kputs("Registered devices:\n");
    kputs("==================\n");
    
    struct device* current = device_list;
    uint32_t count = 0;
    
    while (current)
    {
        kputs("  [");
        kprint_dec(current->id);
        kputs("] ");
        kputs(current->name);
        kputs(" (type: ");
        
        switch (current->type)
        {
            case DEVICE_DISK:
                kputs("DISK");
                break;
            case DEVICE_CHAR:
                kputs("CHAR");
                break;
            case DEVICE_BLOCK:
                kputs("BLOCK");
                break;
            case DEVICE_NETWORK:
                kputs("NETWORK");
                break;
            default:
                kputs("UNKNOWN");
                break;
        }
        
        kputs(", state: ");
        
        switch (current->state)
        {
            case DEVICE_STATE_UNINITIALIZED:
                kputs("UNINITIALIZED");
                break;
            case DEVICE_STATE_INITIALIZED:
                kputs("INITIALIZED");
                break;
            case DEVICE_STATE_READY:
                kputs("READY");
                break;
            case DEVICE_STATE_ERROR:
                kputs("ERROR");
                break;
            case DEVICE_STATE_OFFLINE:
                kputs("OFFLINE");
                break;
        }
        
        kputs(")\n");
        count++;
        current = current->next;
    }
    
    if (count == 0)
        kputs("  (no devices registered)\n");
    else
    {
        kputs("\nTotal: ");
        kprint_dec(count);
        kputs(" device(s)\n");
    }
}

