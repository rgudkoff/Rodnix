#include "../include/vfs.h"
#include "../include/console.h"
#include "../include/common.h"

/* Корневой узел файловой системы */
struct vfs_node* vfs_root = NULL;

/* Список зарегистрированных файловых систем */
static struct vfs_filesystem* fs_list = NULL;

/* Текущая смонтированная файловая система */
static struct vfs_filesystem* mounted_fs = NULL;

/* Инициализация VFS */
int vfs_init(void)
{
    /* Создание корневого узла */
    vfs_root = NULL; /* Будет установлен при монтировании ФС */
    
    kputs("[VFS] Virtual File System initialized\n");
    return 0;
}

/* Регистрация файловой системы */
int vfs_register_filesystem(struct vfs_filesystem* fs)
{
    if (!fs)
        return -1;
    
    /* Проверка, что ФС еще не зарегистрирована */
    struct vfs_filesystem* current = fs_list;
    while (current)
    {
        if (current == fs)
            return -1;
        current = current->next;
    }
    
    /* Добавление в список */
    fs->next = fs_list;
    fs_list = fs;
    
    return 0;
}

/* Монтирование файловой системы */
int vfs_mount(const char* device, const char* mountpoint, const char* fstype)
{
    if (!device || !fstype)
        return -1;
    
    /* Поиск устройства */
    struct device* dev = device_find(device);
    if (!dev)
    {
        kputs("[VFS] Device not found: ");
        kputs(device);
        kputs("\n");
        return -1;
    }
    
    /* Поиск файловой системы */
    struct vfs_filesystem* fs = fs_list;
    while (fs)
    {
        int i = 0;
        int match = 1;
        while (fstype[i] != '\0' && i < 31)
        {
            if (fs->name[i] != fstype[i])
            {
                match = 0;
                break;
            }
            i++;
        }
        if (match && fs->name[i] == '\0')
            break;
        fs = fs->next;
    }
    
    if (!fs)
    {
        kputs("[VFS] Filesystem type not found: ");
        kputs(fstype);
        kputs("\n");
        return -1;
    }
    
    /* Монтирование */
    if (fs->mount)
    {
        int result = fs->mount(dev, mountpoint);
        if (result == 0)
        {
            mounted_fs = fs;
            if (fs->root)
                vfs_root = fs->root;
            
            kputs("[VFS] Mounted ");
            kputs(fstype);
            kputs(" on ");
            kputs(device);
            if (mountpoint)
            {
                kputs(" at ");
                kputs(mountpoint);
            }
            kputs("\n");
            return 0;
        }
        return result;
    }
    
    return -1;
}

/* Размонтирование файловой системы */
int vfs_unmount(const char* mountpoint)
{
    (void)mountpoint; /* Пока не используется */
    
    if (mounted_fs && mounted_fs->unmount)
    {
        int result = mounted_fs->unmount();
        if (result == 0)
        {
            vfs_root = NULL;
            mounted_fs = NULL;
            kputs("[VFS] Filesystem unmounted\n");
            return 0;
        }
        return result;
    }
    
    return -1;
}

/* Открытие файла */
struct vfs_node* vfs_open(const char* path)
{
    if (!path || !vfs_root)
        return NULL;
    
    /* Упрощенная реализация: поиск от корня */
    if (path[0] == '/')
        path++; /* Пропустить начальный '/' */
    
    struct vfs_node* current = vfs_root;
    
    /* Разбор пути (упрощенный) */
    const char* p = path;
    while (*p != '\0' && current)
    {
        /* Поиск следующего '/' */
        const char* end = p;
        while (*end != '\0' && *end != '/')
            end++;
        
        /* Извлечение имени компонента */
        char component[256];
        int i = 0;
        while (p < end && i < 255)
        {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        if (i == 0)
            break;
        
        /* Поиск компонента в текущей директории */
        if (current->finddir)
            current = current->finddir(current, component);
        else
            return NULL;
        
        /* Переход к следующему компоненту */
        if (*p == '/')
            p++;
    }
    
    if (current && current->open)
        current->open(current);
    
    return current;
}

/* Закрытие файла */
int vfs_close(struct vfs_node* node)
{
    if (!node)
        return -1;
    
    if (node->close)
        return node->close(node);
    
    return 0;
}

/* Чтение из файла */
int vfs_read(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer)
{
    if (!node || !buffer)
        return -1;
    
    if (node->read)
        return node->read(node, offset, size, buffer);
    
    return -1;
}

/* Запись в файл */
int vfs_write(struct vfs_node* node, uint32_t offset, uint32_t size, const uint8_t* buffer)
{
    if (!node || !buffer)
        return -1;
    
    if (node->write)
        return node->write(node, offset, size, buffer);
    
    return -1;
}

/* Чтение директории */
struct vfs_node* vfs_readdir(struct vfs_node* node, uint32_t index)
{
    if (!node || node->type != VFS_TYPE_DIRECTORY)
        return NULL;
    
    if (node->readdir)
        return node->readdir(node, index);
    
    return NULL;
}

/* Поиск в директории */
struct vfs_node* vfs_finddir(struct vfs_node* node, const char* name)
{
    if (!node || !name || node->type != VFS_TYPE_DIRECTORY)
        return NULL;
    
    if (node->finddir)
        return node->finddir(node, name);
    
    return NULL;
}

