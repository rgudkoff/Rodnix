#ifndef _RODNIX_PCI_H
#define _RODNIX_PCI_H

#include "types.h"

/* PCI конфигурационные регистры */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

/* Структура PCI устройства */
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint32_t bar[6];  /* Base Address Registers */
} pci_device_t;

/* Инициализация PCI */
int pci_init(void);

/* Сканирование PCI шины */
int pci_scan_bus(void (*callback)(pci_device_t* dev));

/* Чтение конфигурационного регистра PCI */
uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/* Запись конфигурационного регистра PCI */
void pci_write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

/* Поиск устройства по vendor/device ID */
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id);

/* Получение списка всех устройств */
int pci_get_devices(pci_device_t* devices, uint32_t max_devices);

#endif

