/**
 * @file pci.h
 * @brief PCI bus interface
 */

#ifndef _RODNIX_FABRIC_BUS_PCI_H
#define _RODNIX_FABRIC_BUS_PCI_H

#include <stdint.h>
#include <stdbool.h>

#define PCI_BAR_COUNT 6u

typedef struct fabric_device fabric_device_t;
typedef void (*fabric_irq_handler_t)(int vector, void *arg);

typedef struct pci_device_info {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t revision_id;
    uint8_t header_type;
    uint8_t primary_bus;
    uint8_t secondary_bus;
    uint8_t subordinate_bus;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint8_t cap_ptr;
    uint8_t cap_count;
    uint8_t reserved0;
    uint16_t command;
    uint16_t status;
    uint32_t capability_bits;
    uint32_t bars[PCI_BAR_COUNT];
} pci_device_info_t;

typedef struct pci_irq_binding {
    int vector;
    uint8_t mode;
    uint8_t vector_allocated;
    uint8_t reserved0;
    uint8_t reserved1;
} pci_irq_binding_t;

enum {
    PCI_IRQ_MODE_NONE = 0,
    PCI_IRQ_MODE_INTX = 1,
    PCI_IRQ_MODE_MSI  = 2
};

void pci_bus_init(void);
/* Поднять биты в PCI COMMAND устройства (IO/MEM/bus-master). Драйверы
 * включают то, что им нужно, сами — как e1000; это общая ручка для них. */
void pci_command_set(pci_device_info_t* info, uint16_t bits);
int pci_find_capability(fabric_device_t* dev, uint8_t cap_id, uint8_t* out_offset);
bool pci_has_capability(fabric_device_t* dev, uint8_t cap_id);
int pci_irq_vector(fabric_device_t* dev);
int pci_enable_msi(fabric_device_t* dev, uint8_t vector);
int pci_disable_msi(fabric_device_t* dev);
int pci_irq_bind(fabric_device_t* dev,
                 fabric_irq_handler_t handler,
                 void* arg,
                 pci_irq_binding_t* binding);
void pci_irq_unbind(fabric_device_t* dev,
                    fabric_irq_handler_t handler,
                    const pci_irq_binding_t* binding);

#endif /* _RODNIX_FABRIC_BUS_PCI_H */
