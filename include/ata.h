#ifndef _RODNIX_ATA_H
#define _RODNIX_ATA_H

#include "types.h"
#include "device.h"

/* ATA порты */
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW     0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HIGH    0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_ALT_STATUS  0x3F6

#define ATA_SECONDARY_DATA      0x170
#define ATA_SECONDARY_ERROR     0x171
#define ATA_SECONDARY_SECTOR_COUNT 0x172
#define ATA_SECONDARY_LBA_LOW  0x173
#define ATA_SECONDARY_LBA_MID  0x174
#define ATA_SECONDARY_LBA_HIGH 0x175
#define ATA_SECONDARY_DRIVE    0x176
#define ATA_SECONDARY_COMMAND  0x177
#define ATA_SECONDARY_STATUS   0x177
#define ATA_SECONDARY_ALT_STATUS 0x376

/* ATA команды */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT   0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT  0x34
#define ATA_CMD_IDENTIFY        0xEC

/* ATA статус регистр */
#define ATA_SR_BSY  0x80  /* Busy */
#define ATA_SR_DRDY 0x40  /* Drive ready */
#define ATA_SR_DF   0x20  /* Drive write fault */
#define ATA_SR_DSC  0x10  /* Drive seek complete */
#define ATA_SR_DRQ  0x08  /* Data request ready */
#define ATA_SR_CORR 0x04  /* Corrected data */
#define ATA_SR_IDX  0x02  /* Index */
#define ATA_SR_ERR  0x01  /* Error */

/* ATA ошибки */
#define ATA_ER_BBK  0x80  /* Bad block */
#define ATA_ER_UNC  0x40  /* Uncorrectable data */
#define ATA_ER_MC   0x20  /* Media changed */
#define ATA_ER_IDNF 0x10  /* ID not found */
#define ATA_ER_MCR  0x08  /* Media change requested */
#define ATA_ER_ABRT 0x04  /* Command aborted */
#define ATA_ER_TK0NF 0x02 /* Track 0 not found */
#define ATA_ER_AMNF 0x01  /* Address mark not found */

/* Структура данных ATA устройства */
struct ata_device {
    uint16_t base_port;        /* Базовый порт (PRIMARY или SECONDARY) */
    uint8_t drive;             /* 0 = master, 1 = slave */
    uint32_t sectors;          /* Количество секторов */
    uint32_t sector_size;      /* Размер сектора (обычно 512) */
    uint8_t present;           /* Устройство присутствует */
};

/* Функции ATA */
int ata_init(struct device* dev);
int ata_read_sector(struct device* dev, uint32_t lba, uint8_t* buffer);
int ata_write_sector(struct device* dev, uint32_t lba, const uint8_t* buffer);
int ata_read(struct device* dev, void* buffer, uint32_t offset, uint32_t size);
int ata_write(struct device* dev, const void* buffer, uint32_t offset, uint32_t size);

/* Регистрация ATA устройств */
int ata_register_devices(void);

#endif

