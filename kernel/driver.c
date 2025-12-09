#include "../include/driver.h"
#include "../include/console.h"
#include "../include/common.h"

/* Список зарегистрированных драйверов */
static struct driver* driver_list = NULL;

/* Регистрация драйвера */
int driver_register(struct driver* drv)
{
    if (!drv || !drv->name)
        return -1;
    
    /* Проверка, не зарегистрирован ли уже */
    struct driver* current = driver_list;
    while (current)
    {
        int i = 0;
        int match = 1;
        while (drv->name[i] != '\0' && current->name[i] != '\0')
        {
            if (drv->name[i] != current->name[i])
            {
                match = 0;
                break;
            }
            i++;
        }
        if (match && drv->name[i] == '\0' && current->name[i] == '\0')
        {
            kputs("[DRIVER] Driver already registered: ");
            kputs(drv->name);
            kputs("\n");
            return -1;
        }
        current = current->next;
    }
    
    /* Добавление в список */
    drv->next = driver_list;
    driver_list = drv;
    
    kputs("[DRIVER] Registered: ");
    kputs(drv->name);
    kputs(" (version ");
    kprint_dec(drv->version);
    kputs(")\n");
    
    return 0;
}

/* Поиск драйвера по имени */
struct driver* driver_find(const char* name)
{
    if (!name)
        return NULL;
    
    struct driver* current = driver_list;
    while (current)
    {
        int i = 0;
        int match = 1;
        while (name[i] != '\0' && current->name[i] != '\0')
        {
            if (name[i] != current->name[i])
            {
                match = 0;
                break;
            }
            i++;
        }
        if (match && name[i] == '\0' && current->name[i] == '\0')
            return current;
        
        current = current->next;
    }
    
    return NULL;
}

/* Поиск драйвера по типу устройства */
struct driver* driver_find_by_type(device_type_t type)
{
    struct driver* current = driver_list;
    while (current)
    {
        if (current->device_type == type)
            return current;
        current = current->next;
    }
    
    return NULL;
}

/* Список всех драйверов */
void driver_list_all(void)
{
    kputs("Registered drivers:\n");
    kputs("==================\n");
    
    struct driver* current = driver_list;
    uint32_t count = 0;
    
    while (current)
    {
        kputs("  [");
        kprint_dec(count + 1);
        kputs("] ");
        kputs(current->name);
        kputs(" (v");
        kprint_dec(current->version);
        kputs(", type: ");
        
        switch (current->device_type)
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
        
        kputs(")\n");
        count++;
        current = current->next;
    }
    
    if (count == 0)
        kputs("  (no drivers registered)\n");
    else
    {
        kputs("\nTotal: ");
        kprint_dec(count);
        kputs(" driver(s)\n");
    }
}

/* Инициализация системы драйверов */
int driver_system_init(void)
{
    driver_list = NULL;
    kputs("[DRIVER] Driver system initialized\n");
    return 0;
}

/* Загрузка всех зарегистрированных драйверов */
int driver_load_all(void)
{
    struct driver* current = driver_list;
    uint32_t loaded = 0;
    uint32_t failed = 0;
    
    while (current)
    {
        kputs("[DRIVER] Loading driver: ");
        kputs(current->name);
        kputs("\n");
        
        /* Инициализация драйвера */
        if (current->init)
        {
            int result = current->init();
            if (result != 0)
            {
                kputs("[DRIVER] Initialization failed: ");
                kputs(current->name);
                kputs("\n");
                failed++;
            }
            else
            {
                kputs("[DRIVER] Initialized: ");
                kputs(current->name);
                kputs("\n");
            }
        }
        
        /* Поиск устройств (probe) */
        if (current->probe)
        {
            int result = current->probe();
            if (result != 0)
            {
                kputs("[DRIVER] Probe failed: ");
                kputs(current->name);
                kputs("\n");
            }
            else
            {
                kputs("[DRIVER] Probe successful: ");
                kputs(current->name);
                kputs("\n");
            }
        }
        
        loaded++;
        current = current->next;
    }
    
    kputs("[DRIVER] Loaded ");
    kprint_dec(loaded);
    kputs(" driver(s), ");
    kprint_dec(failed);
    kputs(" failed\n");
    
    return (failed == 0) ? 0 : -1;
}


