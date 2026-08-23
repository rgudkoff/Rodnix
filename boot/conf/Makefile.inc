# nasm warns about the zero-extended relocation the Multiboot2 header needs
# to reach a 64-bit symbol from 32-bit boot code. The header is correct; the
# warning is not actionable, and it is specific to this one file.
boot/boot.S_ASFLAGS_ADD += -Wno-zext-reloc
