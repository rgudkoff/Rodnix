#ifndef _RODNIX_VFS_H
#define _RODNIX_VFS_H

#include "types.h"
#include "device.h"

/* Типы файлов */
typedef enum {
    VFS_TYPE_UNKNOWN = 0,
    VFS_TYPE_FILE,
    VFS_TYPE_DIRECTORY,
    VFS_TYPE_DEVICE,
    VFS_TYPE_SYMLINK
} vfs_type_t;

/* Права доступа (упрощенные) */
typedef enum {
    VFS_MODE_READ   = 0x01,
    VFS_MODE_WRITE  = 0x02,
    VFS_MODE_EXEC   = 0x04
} vfs_mode_t;

/* Структура файла/директории */
struct vfs_node {
    char name[256];             /* Имя файла/директории */
    vfs_type_t type;           /* Тип узла */
    vfs_mode_t mode;           /* Права доступа */
    
    uint32_t size;             /* Размер файла (для файлов) */
    uint32_t inode;            /* Inode номер */
    
    /* Методы файловой системы */
    int (*read)(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    int (*write)(struct vfs_node* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
    int (*open)(struct vfs_node* node);
    int (*close)(struct vfs_node* node);
    struct vfs_node* (*readdir)(struct vfs_node* node, uint32_t index);
    struct vfs_node* (*finddir)(struct vfs_node* node, const char* name);
    
    /* Данные файловой системы */
    void* fs_data;             /* Данные конкретной ФС */
    struct device* device;     /* Устройство, на котором находится файл */
    
    /* Связанные узлы */
    struct vfs_node* parent;   /* Родительская директория */
    struct vfs_node* next;     /* Следующий узел в списке */
};

/* Корневая файловая система */
struct vfs_filesystem {
    char name[32];             /* Имя ФС (например, "initrd", "ext2") */
    struct vfs_node* root;     /* Корневой узел */
    struct device* device;     /* Устройство, на котором находится ФС */
    
    /* Методы ФС */
    int (*mount)(struct device* dev, const char* mountpoint);
    int (*unmount)(void);
    
    struct vfs_filesystem* next;
};

/* Функции VFS */
int vfs_init(void);
int vfs_mount(const char* device, const char* mountpoint, const char* fstype);
int vfs_unmount(const char* mountpoint);
struct vfs_node* vfs_open(const char* path);
int vfs_close(struct vfs_node* node);
int vfs_read(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int vfs_write(struct vfs_node* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
struct vfs_node* vfs_readdir(struct vfs_node* node, uint32_t index);
struct vfs_node* vfs_finddir(struct vfs_node* node, const char* name);

/* Регистрация файловой системы */
int vfs_register_filesystem(struct vfs_filesystem* fs);

/* Глобальные переменные */
extern struct vfs_node* vfs_root;

#endif

