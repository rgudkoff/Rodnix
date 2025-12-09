# RodNIX


A microkernel-based OS for i386 (GRUB2 multiboot2) with strict separation between kernel and userspace.

**Architecture:**
- **Kernel**: C11, no stdio, minimal dependencies - provides scheduler, VM, IPC, capabilities, IRQ routing, bus enumeration
- **Userspace**: Rust 1.75+ - drivers (no_std) and daemons (std)
- **Security**: W^X, NX bit, KPTI-like address space separation

## Structure
- `boot/` — entry point, multiboot2, stacks, GDT/IDT/ISR stubs.
- `kernel/` — kernel: GDT/IDT/ISR/IRQ/PIC/PIT/keyboard, kmain.
- `drivers/` — simple drivers for VGA console and I/O ports.
- `include/` — headers.
- `link.ld` — linking for `elf32-i386`, loading at 1 MiB.
- `grub.cfg` — ISO menu.
- `Makefile` — kernel build, ISO, QEMU launch.

## Requirements
- `i686-elf-gcc`, `i686-elf-ld`
- `nasm`
- `grub-mkrescue`, `xorriso`
- `qemu-system-i386`

Make sure the tools are available in PATH. On macOS, it's convenient to install via `brew install i686-elf-gcc nasm xorriso qemu` and `brew install --cask gcc@<version>`, then add `i686-elf-*` to PATH.

## Building
```sh
cd /Users/romangudkov/Rodnix
make
```
Result: `build/rodnix.kernel`.

## ISO and Running
```sh
make run          # builds ISO and launches QEMU (-serial stdio)
```
ISO file: `rodnix.iso`, kernel placement in `iso/boot/rodnix.kernel`.

## Debugging
```sh
make debug        # QEMU -s -S, then
gdb build/rodnix.kernel
```

## Current Status
- Multiboot2 header, freestanding 32-bit.
- GDT/IDT/ISR/IRQ, PIC remap to 0x20/0x28.
- PIT 100 Hz (tick counter).
- Keyboard handler (keymap, character output).
- VGA console 80x25.
- **Device Management System (Device Manager)**:
  - Device registration and search by name/type
  - Unified interface for working with devices
  - Device states (uninitialized, initialized, ready, error, offline)
- **ATA/IDE Driver**:
  - Sector read/write support
  - Disk initialization and detection
  - Working with PRIMARY channel (master/slave)
- **VFS (Virtual File System)**:
  - File system abstraction
  - Mount/unmount support
  - Interfaces for working with files and directories
- **Shell Commands**:
  - `devices` - list of registered devices
  - `meminfo` - memory information (physical, virtual, heap)
- **Memory Manager**:
  - **PMM (Physical Memory Manager)**: physical page management via bitmap
  - **Paging**: paged addressing with support for page directory and page tables
  - **VMM (Virtual Memory Manager)**: virtual address management
  - **Heap Allocator**: dynamic memory allocation for kernel (kmalloc/kfree/krealloc)
  - Kernel mapping to virtual memory at address 0xC0000000

## Plans
- Implementation of a simple file system (initrd or simple format)
- Getting memory information from Multiboot2
- Extending VMM for process support
- Extending VFS for working with real file systems



The MIT License allows anyone to use, modify, and distribute this software, including for commercial purposes. However, the copyright holder (the original author) maintains control over the main repository and has the final say on what changes are accepted into the official codebase.

For more details, see the [LICENSE](LICENSE) file.
