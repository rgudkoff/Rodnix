#ifndef _RODNIX_COMPAT_DEV_PCI_PCIREG_H
#define _RODNIX_COMPAT_DEV_PCI_PCIREG_H

/* Common class/subclass values used by many drivers. */
#define PCIC_STORAGE 0x01
#define PCIC_NETWORK 0x02
#define PCIC_DISPLAY 0x03
#define PCIC_BRIDGE  0x06

#define PCIS_STORAGE_IDE 0x01
#define PCIS_NETWORK_ETHERNET 0x00
#define PCIS_BRIDGE_HOST 0x00
#define PCIS_BRIDGE_ISA  0x01
#define PCIS_BRIDGE_PCI  0x04

#define PCIR_COMMAND 0x04
#define PCIR_STATUS  0x06
#define PCIR_CAP_PTR 0x34

#define PCIM_CMD_INTxDIS 0x0400

#define PCI_STATUS_CAP_LIST 0x0010

#define PCIY_MSI     0x05
#define PCIY_EXPRESS 0x10
#define PCIY_MSIX    0x11

#define PCIR_MSI_CTRL       0x02
#define PCIR_MSI_ADDR       0x04
#define PCIR_MSI_ADDR_HIGH  0x08
#define PCIR_MSI_DATA_32    0x08
#define PCIR_MSI_DATA_64    0x0C

#define PCIM_MSICTRL_MSI_ENABLE 0x0001
#define PCIM_MSICTRL_64BIT      0x0080

#endif /* _RODNIX_COMPAT_DEV_PCI_PCIREG_H */
