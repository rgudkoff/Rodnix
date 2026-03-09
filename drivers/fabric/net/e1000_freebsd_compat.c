/**
 * @file e1000_freebsd_compat.c
 * @brief RodNIX compatibility hooks required by FreeBSD shared e1000 core.
 */

#include "../../../include/dev/pci/pcireg.h"
#include "../../../include/dev/pci/pcivar.h"
#include "../../../third_party/freebsd/sys/dev/e1000/e1000_api.h"

int e1000_use_pause_delay = 0;

void e1000_write_pci_cfg(struct e1000_hw* hw, u32 reg, u16* value)
{
    struct e1000_osdep* osdep = (struct e1000_osdep*)hw->back;
    pci_write_config(osdep->dev, reg, *value, 2);
}

void e1000_read_pci_cfg(struct e1000_hw* hw, u32 reg, u16* value)
{
    struct e1000_osdep* osdep = (struct e1000_osdep*)hw->back;
    *value = (u16)pci_read_config(osdep->dev, reg, 2);
}

void e1000_pci_set_mwi(struct e1000_hw* hw)
{
    struct e1000_osdep* osdep = (struct e1000_osdep*)hw->back;
    u16 cmd = (u16)pci_read_config(osdep->dev, PCIR_COMMAND, 2);
    cmd = (u16)(cmd | CMD_MEM_WRT_INVALIDATE);
    pci_write_config(osdep->dev, PCIR_COMMAND, cmd, 2);
}

void e1000_pci_clear_mwi(struct e1000_hw* hw)
{
    struct e1000_osdep* osdep = (struct e1000_osdep*)hw->back;
    u16 cmd = (u16)pci_read_config(osdep->dev, PCIR_COMMAND, 2);
    cmd = (u16)(cmd & (u16)~CMD_MEM_WRT_INVALIDATE);
    pci_write_config(osdep->dev, PCIR_COMMAND, cmd, 2);
}

s32 e1000_read_pcie_cap_reg(struct e1000_hw* hw, u32 reg, u16* value)
{
    struct e1000_osdep* osdep = (struct e1000_osdep*)hw->back;
    u32 capoff = 0;
    if (pci_find_cap(osdep->dev, PCIY_EXPRESS, &capoff) != 0) {
        return E1000_ERR_CONFIG;
    }
    *value = (u16)pci_read_config(osdep->dev, capoff + reg, 2);
    return E1000_SUCCESS;
}

s32 e1000_write_pcie_cap_reg(struct e1000_hw* hw, u32 reg, u16* value)
{
    struct e1000_osdep* osdep = (struct e1000_osdep*)hw->back;
    u32 capoff = 0;
    if (pci_find_cap(osdep->dev, PCIY_EXPRESS, &capoff) != 0) {
        return E1000_ERR_CONFIG;
    }
    pci_write_config(osdep->dev, capoff + reg, *value, 2);
    return E1000_SUCCESS;
}
