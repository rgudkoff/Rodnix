# boot - Boot Code

This directory contains boot code for different architectures.

## Structure

```
boot/
├── boot.S          # x86_64 boot code (Multiboot2)
└── README.md       # This file
```

## x86_64 Boot Process

1. **Multiboot2 Entry**: Bootloader loads kernel and jumps to `start`
2. **32-bit Setup**: Initialize page tables, enable PAE
3. **64-bit Switch**: Enable long mode, switch to 64-bit code
4. **Kernel Entry**: Call `kmain()` with boot information

## Page Tables

Early boot sets up identity mapping for first 1GiB:
- PML4 → PDPT → PD (2MiB pages)
- Allows smooth transition to 64-bit mode

## Future

- ARM64 boot code
- RISC-V64 boot code
- UEFI support
- Device tree support

