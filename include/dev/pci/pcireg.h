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

#endif /* _RODNIX_COMPAT_DEV_PCI_PCIREG_H */
