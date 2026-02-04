# boot - Boot Code

This directory contains boot code for different architectures.

## Structure

- boot/boot.S: x86_64 boot code (Multiboot2)
- boot/README.md: this file

## x86_64 Boot Process

1. Multiboot2 entry: bootloader loads kernel and jumps to start
2. 32-bit setup: initialize page tables, enable PAE
3. 64-bit switch: enable long mode, switch to 64-bit code
4. Kernel entry: call kmain() with boot information

## Page Tables

Early boot sets up identity mapping for the first 1 GiB:
- PML4 to PDPT to PD (2 MiB pages)
- Allows smooth transition to 64-bit mode

## Future

- ARM64 boot code
- RISC-V64 boot code
- UEFI support
- Device tree support
