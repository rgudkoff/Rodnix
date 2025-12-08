#include "../include/ata.h"
#include "../include/ports.h"
#include "../include/console.h"
#include "../include/common.h"
#include "../include/device.h"
#include "../include/driver.h"

/* Ожидание готовности ATA устройства с таймаутом */
static int ata_wait_ready(uint16_t base_port)
{
    uint16_t status_port = base_port + ATA_PRIMARY_STATUS - ATA_PRIMARY_DATA;
    uint32_t timeout = 100000; /* Таймаут */
    
    /* Ждем, пока устройство не будет готово */
    while ((inb(status_port) & ATA_SR_BSY) && --timeout)
        ;
    
    if (timeout == 0)
        return -1; /* Таймаут */
    
    /* Ждем, пока устройство не будет готово к передаче данных */
    timeout = 100000;
    while (!(inb(status_port) & ATA_SR_DRDY) && --timeout)
        ;
    
    if (timeout == 0)
        return -1; /* Таймаут */
    
    return 0; /* Успех */
}

/* Ожидание готовности к передаче данных с таймаутом */
static int ata_wait_data(uint16_t base_port)
{
    if (ata_wait_ready(base_port) != 0)
        return -1;
    
    uint16_t status_port = base_port + ATA_PRIMARY_STATUS - ATA_PRIMARY_DATA;
    uint32_t timeout = 100000;
    
    /* Ждем DRQ (Data Request) */
    while (!(inb(status_port) & ATA_SR_DRQ) && --timeout)
        ;
    
    if (timeout == 0)
        return -1; /* Таймаут */
    
    return 0; /* Успех */
}

/* Инициализация ATA устройства */
int ata_init(struct device* dev)
{
    if (!dev || !dev->private_data)
        return -1;
    
    struct ata_device* ata = (struct ata_device*)dev->private_data;
    
    /* Выбор диска */
    uint8_t drive_sel = 0xE0 | (ata->drive << 4);
    outb(ata->base_port + ATA_PRIMARY_DRIVE - ATA_PRIMARY_DATA, drive_sel);
    
    /* Небольшая задержка */
    for (volatile int i = 0; i < 1000; i++)
        ;
    
    /* Ожидание готовности */
    if (ata_wait_ready(ata->base_port) != 0)
    {
        ata->present = 0;
        return -1; /* Устройство не отвечает */
    }
    
    /* Отправка команды IDENTIFY */
    outb(ata->base_port + ATA_PRIMARY_SECTOR_COUNT - ATA_PRIMARY_DATA, 0);
    outb(ata->base_port + ATA_PRIMARY_LBA_LOW - ATA_PRIMARY_DATA, 0);
    outb(ata->base_port + ATA_PRIMARY_LBA_MID - ATA_PRIMARY_DATA, 0);
    outb(ata->base_port + ATA_PRIMARY_LBA_HIGH - ATA_PRIMARY_DATA, 0);
    outb(ata->base_port + ATA_PRIMARY_COMMAND - ATA_PRIMARY_DATA, ATA_CMD_IDENTIFY);
    
    /* Небольшая задержка */
    for (volatile int i = 0; i < 1000; i++)
        ;
    
    /* Проверка наличия устройства */
    uint8_t status = inb(ata->base_port + ATA_PRIMARY_STATUS - ATA_PRIMARY_DATA);
    if (status == 0 || status == 0xFF)
    {
        ata->present = 0;
        return -1; /* Устройство не найдено */
    }
    
    /* Ожидание готовности */
    if (ata_wait_ready(ata->base_port) != 0)
    {
        ata->present = 0;
        return -1; /* Таймаут */
    }
    
    /* Проверка ошибки */
    status = inb(ata->base_port + ATA_PRIMARY_STATUS - ATA_PRIMARY_DATA);
    if (status & ATA_SR_ERR)
    {
        ata->present = 0;
        return -1;
    }
    
    /* Проверка, что устройство готово к передаче данных */
    if (!(status & ATA_SR_DRQ))
    {
        ata->present = 0;
        return -1; /* Устройство не готово */
    }
    
    /* Чтение данных IDENTIFY */
    uint16_t identify[256];
    for (int i = 0; i < 256; i++)
    {
        if (ata_wait_data(ata->base_port) != 0)
        {
            ata->present = 0;
            return -1; /* Таймаут при чтении */
        }
        identify[i] = inw(ata->base_port);
    }
    
    /* Извлечение информации о диске */
    ata->sectors = *(uint32_t*)(&identify[60]); /* LBA28 sectors */
    ata->sector_size = 512; /* Стандартный размер сектора */
    ata->present = 1;
    
    return 0;
}

/* Чтение сектора */
int ata_read_sector(struct device* dev, uint32_t lba, uint8_t* buffer)
{
    if (!dev || !dev->private_data || !buffer)
        return -1;
    
    struct ata_device* ata = (struct ata_device*)dev->private_data;
    
    if (!ata->present)
        return -1;
    
    /* Выбор диска */
    uint8_t drive_sel = 0xE0 | (ata->drive << 4) | ((lba >> 24) & 0x0F);
    outb(ata->base_port + ATA_PRIMARY_DRIVE - ATA_PRIMARY_DATA, drive_sel);
    
    /* Ожидание готовности */
    if (ata_wait_ready(ata->base_port) != 0)
        return -1;
    
    /* Отправка параметров LBA */
    outb(ata->base_port + ATA_PRIMARY_SECTOR_COUNT - ATA_PRIMARY_DATA, 1);
    outb(ata->base_port + ATA_PRIMARY_LBA_LOW - ATA_PRIMARY_DATA, (uint8_t)(lba & 0xFF));
    outb(ata->base_port + ATA_PRIMARY_LBA_MID - ATA_PRIMARY_DATA, (uint8_t)((lba >> 8) & 0xFF));
    outb(ata->base_port + ATA_PRIMARY_LBA_HIGH - ATA_PRIMARY_DATA, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Отправка команды чтения */
    outb(ata->base_port + ATA_PRIMARY_COMMAND - ATA_PRIMARY_DATA, ATA_CMD_READ_PIO);
    
    /* Ожидание готовности данных */
    if (ata_wait_data(ata->base_port) != 0)
        return -1;
    
    /* Чтение сектора (256 слов = 512 байт) */
    for (int i = 0; i < 256; i++)
    {
        uint16_t word = inw(ata->base_port);
        buffer[i * 2] = (uint8_t)(word & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)((word >> 8) & 0xFF);
    }
    
    return 0;
}

/* Запись сектора */
int ata_write_sector(struct device* dev, uint32_t lba, const uint8_t* buffer)
{
    if (!dev || !dev->private_data || !buffer)
        return -1;
    
    struct ata_device* ata = (struct ata_device*)dev->private_data;
    
    if (!ata->present)
        return -1;
    
    /* Выбор диска */
    uint8_t drive_sel = 0xE0 | (ata->drive << 4) | ((lba >> 24) & 0x0F);
    outb(ata->base_port + ATA_PRIMARY_DRIVE - ATA_PRIMARY_DATA, drive_sel);
    
    /* Ожидание готовности */
    if (ata_wait_ready(ata->base_port) != 0)
        return -1;
    
    /* Отправка параметров LBA */
    outb(ata->base_port + ATA_PRIMARY_SECTOR_COUNT - ATA_PRIMARY_DATA, 1);
    outb(ata->base_port + ATA_PRIMARY_LBA_LOW - ATA_PRIMARY_DATA, (uint8_t)(lba & 0xFF));
    outb(ata->base_port + ATA_PRIMARY_LBA_MID - ATA_PRIMARY_DATA, (uint8_t)((lba >> 8) & 0xFF));
    outb(ata->base_port + ATA_PRIMARY_LBA_HIGH - ATA_PRIMARY_DATA, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Отправка команды записи */
    outb(ata->base_port + ATA_PRIMARY_COMMAND - ATA_PRIMARY_DATA, ATA_CMD_WRITE_PIO);
    
    /* Ожидание готовности данных */
    if (ata_wait_data(ata->base_port) != 0)
        return -1;
    
    /* Запись сектора (256 слов = 512 байт) */
    for (int i = 0; i < 256; i++)
    {
        uint16_t word = ((uint16_t)buffer[i * 2 + 1] << 8) | buffer[i * 2];
        outw(ata->base_port, word);
    }
    
    /* Ожидание завершения записи */
    if (ata_wait_ready(ata->base_port) != 0)
        return -1;
    
    return 0;
}

/* Чтение данных (общий интерфейс устройства) */
int ata_read(struct device* dev, void* buffer, uint32_t offset, uint32_t size)
{
    if (!dev || !dev->private_data || !buffer)
        return -1;
    
    struct ata_device* ata = (struct ata_device*)dev->private_data;
    
    if (!ata->present)
        return -1;
    
    uint32_t sector_size = ata->sector_size;
    uint32_t start_sector = offset / sector_size;
    uint32_t end_sector = (offset + size + sector_size - 1) / sector_size;
    uint32_t sectors_to_read = end_sector - start_sector;
    
    uint8_t* sector_buffer = (uint8_t*)buffer;
    uint32_t bytes_read = 0;
    
    for (uint32_t i = 0; i < sectors_to_read; i++)
    {
        uint32_t lba = start_sector + i;
        uint8_t temp_sector[512];
        
        if (ata_read_sector(dev, lba, temp_sector) != 0)
            return -1;
        
        uint32_t copy_offset = (i == 0) ? (offset % sector_size) : 0;
        uint32_t copy_size = sector_size - copy_offset;
        if (bytes_read + copy_size > size)
            copy_size = size - bytes_read;
        
        /* Копирование данных */
        for (uint32_t j = 0; j < copy_size; j++)
        {
            sector_buffer[bytes_read + j] = temp_sector[copy_offset + j];
        }
        
        bytes_read += copy_size;
    }
    
    return bytes_read;
}

/* Запись данных (общий интерфейс устройства) */
int ata_write(struct device* dev, const void* buffer, uint32_t offset, uint32_t size)
{
    if (!dev || !dev->private_data || !buffer)
        return -1;
    
    struct ata_device* ata = (struct ata_device*)dev->private_data;
    
    if (!ata->present)
        return -1;
    
    uint32_t sector_size = ata->sector_size;
    uint32_t start_sector = offset / sector_size;
    uint32_t end_sector = (offset + size + sector_size - 1) / sector_size;
    uint32_t sectors_to_write = end_sector - start_sector;
    
    const uint8_t* sector_buffer = (const uint8_t*)buffer;
    uint32_t bytes_written = 0;
    
    for (uint32_t i = 0; i < sectors_to_write; i++)
    {
        uint32_t lba = start_sector + i;
        uint8_t temp_sector[512];
        
        /* Если это не полный сектор, нужно сначала прочитать существующие данные */
        uint32_t copy_offset = (i == 0) ? (offset % sector_size) : 0;
        if (copy_offset != 0 || (i == sectors_to_write - 1 && (offset + size) % sector_size != 0))
        {
            if (ata_read_sector(dev, lba, temp_sector) != 0)
                return -1;
        }
        
        /* Копирование данных в сектор */
        uint32_t copy_size = sector_size - copy_offset;
        if (bytes_written + copy_size > size)
            copy_size = size - bytes_written;
        
        for (uint32_t j = 0; j < copy_size; j++)
        {
            temp_sector[copy_offset + j] = sector_buffer[bytes_written + j];
        }
        
        /* Запись сектора */
        if (ata_write_sector(dev, lba, temp_sector) != 0)
            return -1;
        
        bytes_written += copy_size;
    }
    
    return bytes_written;
}

/* Регистрация ATA устройств */
int ata_register_devices(void)
{
    /* Создание устройств для PRIMARY канала (master и slave) */
    static struct device ata_primary_master;
    static struct ata_device ata_primary_master_data;
    
    ata_primary_master_data.base_port = ATA_PRIMARY_DATA;
    ata_primary_master_data.drive = 0; /* Master */
    ata_primary_master_data.sectors = 0;
    ata_primary_master_data.sector_size = 512;
    ata_primary_master_data.present = 0;
    
    /* Инициализация имени устройства */
    int i = 0;
    const char* name = "ata0";
    while (name[i] != '\0' && i < 31)
    {
        ata_primary_master.name[i] = name[i];
        i++;
    }
    ata_primary_master.name[i] = '\0';
    
    ata_primary_master.type = DEVICE_DISK;
    ata_primary_master.state = DEVICE_STATE_UNINITIALIZED;
    ata_primary_master.id = 0;
    ata_primary_master.init = ata_init;
    ata_primary_master.read = ata_read;
    ata_primary_master.write = ata_write;
    ata_primary_master.ioctl = NULL;
    ata_primary_master.close = NULL;
    ata_primary_master.private_data = &ata_primary_master_data;
    ata_primary_master.next = NULL;
    
    /* Регистрация устройства */
    /* Инициализация будет выполнена автоматически, но с таймаутами не зависнет, если устройства нет */
    if (device_register(&ata_primary_master) == 0)
    {
        if (ata_primary_master.state == DEVICE_STATE_READY)
        {
            kputs("[ATA] Registered and initialized: ata0 (PRIMARY master)\n");
        }
        else
        {
            kputs("[ATA] Registered: ata0 (PRIMARY master, not present or failed to initialize)\n");
        }
    }
    
    return 0;
}

/* Драйвер ATA */
static struct driver ata_driver = {
    .name = "ata",
    .version = 1,
    .device_type = DEVICE_DISK,
    .init = NULL,
    .exit = NULL,
    .probe = ata_register_devices,
    .next = NULL
};

/* Регистрация драйвера при загрузке (используем конструктор GCC) */
__attribute__((constructor))
static void ata_driver_register(void)
{
    driver_register(&ata_driver);
}

