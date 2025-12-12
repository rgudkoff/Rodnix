# Building and Running RodNIX

## Prerequisites

### Required Tools

1. **Cross-compiler toolchain** (x86_64-elf-gcc, x86_64-elf-ld)
   - macOS: `brew install x86_64-elf-gcc x86_64-elf-binutils`
   - Linux: Install from your distribution's package manager

2. **NASM** (assembler)
   - macOS: `brew install nasm`
   - Linux: `sudo apt-get install nasm`

3. **QEMU** (for running the kernel)
   - macOS: `brew install qemu`
   - Linux: `sudo apt-get install qemu-system-x86`

4. **GRUB** (for creating bootable ISO) - Optional but recommended
   - macOS: `brew install grub`
   - Linux: `sudo apt-get install grub-pc-bin`

## Building

### Basic Build

```bash
make clean
make
```

This will create `build/rodnix.kernel`.

### Create Bootable ISO

```bash
make iso
```

This requires `grub-mkrescue` to be installed.

### Run in QEMU

```bash
make run
```

If ISO exists, it will boot from ISO. Otherwise, it will attempt direct kernel load (may not work without GRUB).

## Troubleshooting

### "grub-mkrescue not found"

Install GRUB:
- macOS: `brew install grub`
- Linux: `sudo apt-get install grub-pc-bin`

### "qemu-system-x86_64 not found"

Install QEMU:
- macOS: `brew install qemu`
- Linux: `sudo apt-get install qemu-system-x86`

### "x86_64-elf-gcc not found"

Install cross-compiler:
- macOS: `brew install x86_64-elf-gcc x86_64-elf-binutils`
- Linux: Install from your distribution's package manager

## Manual QEMU Run

If you have an ISO:

```bash
qemu-system-x86_64 -m 64M -boot d -cdrom rodnix.iso -serial stdio
```

## Debugging

Build with debug symbols:

```bash
make debug
```

Then use GDB to debug:

```bash
qemu-system-x86_64 -m 64M -kernel build/rodnix.kernel -s -S &
gdb build/rodnix.kernel
(gdb) target remote :1234
```

