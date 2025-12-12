# RodNIX

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A microkernel-based OS for x86_64 (GRUB2 multiboot2) with strict separation between kernel and userspace.

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
- `x86_64-elf-gcc`, `x86_64-elf-ld`
- `nasm`
- `grub-mkrescue`, `xorriso`
- `qemu-system-x86_64`

Make sure the tools are available in PATH. On macOS, it's convenient to install via `brew install x86_64-elf-gcc nasm xorriso qemu` and `brew install --cask gcc@<version>`, then add `x86_64-elf-*` to PATH.

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
- Multiboot2 header, freestanding 64-bit (x86_64).
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
  - **PMM (Physical Memory Manager)**: physical page management via bitmap (64-bit addresses)
  - **Paging**: fully functional 64-bit paged addressing with 4-level page tables
    - PML4 → PDPT → PD → PT structure (64-bit)
    - Identity mapping for first 4MB (0x0-0x3FFFFF)
    - High-half kernel mapping at 0xFFFFFFFF80000000 (canonical address)
    - Page fault handler for debugging
    - Support for 40-bit physical addresses (up to 1TB)
  - **VMM (Virtual Memory Manager)**: virtual address management
  - **Heap Allocator**: dynamic memory allocation for kernel (kmalloc/kfree/krealloc)

## Paging Implementation (64-bit)

The kernel implements a complete 64-bit paging system with the following features:

- **4-Level Page Tables**: PML4 (512 entries) → PDPT (512 entries) → PD (512 entries) → PT (512 entries)
- **64-bit Entries**: All page table entries are 64-bit (8 bytes), supporting 40-bit physical addresses (up to 1TB)
- **Identity Mapping**: First 4MB (0x0-0x3FFFFF) are identity-mapped for smooth transition
- **High-Half Mapping**: Kernel code is mapped to canonical virtual addresses 0xFFFFFFFF80100000-0xFFFFFFFF804FFFFF (physical 0x100000-0x4FFFFF)
- **Double Mapping**: Both identity and high-half mappings exist simultaneously for safe transition
- **PAE Required**: PAE (Physical Address Extension) is automatically enabled for 64-bit mode
- **Page Fault Handler**: Debug handler that displays fault address, error code, and system state
- **Safety Checks**: Comprehensive validation before enabling paging:
  - CR3 alignment check (must be 4KB aligned)
  - PML4/PDPT/PD/PT flags verification
  - Identity mapping verification for code and stack
  - Page tables mapping verification

**Current Status**: 
- ✅ 64-bit paging is fully functional with identity mapping
- ✅ High-half mapping (0xFFFFFFFF80000000+) is created and ready
- ✅ Function `paging_jump_to_high_half()` is implemented for transition
- ⏳ Automatic transition to high-half is planned (currently manual call required)

## Plans
- High-half kernel mapping (0xC0000000+) with double mapping for smooth transition
- Implementation of a simple file system (initrd or simple format)
- Getting memory information from Multiboot2
- Extending VMM for process support
- Extending VFS for working with real file systems

## License

This project is licensed under the [MIT License](LICENSE).

The MIT License allows anyone to use, modify, and distribute this software, including for commercial purposes. However, the copyright holder (the original author) maintains control over the main repository and has the final say on what changes are accepted into the official codebase.

For more details, see the [LICENSE](LICENSE) file.


# RodNIX Licensing FAQ

Q: Is RodNIX free?
A: Yes — for non-commercial use and community development.

Q: Can indie developers sell RodNIX-based products?
A: Yes. Indie and small-team commercial use is allowed for free.

Q: Do enterprises need to pay?
A: Yes. Any enterprise use requires an Enterprise License (REL-1.0).

Q: Why pay?
A: Revenue funds the RodNIX Foundation and is distributed to contributors.

Q: Do contributors keep copyright?
A: Yes.

Q: Why must contributors sign a CLA?
A: To allow commercial dual-licensing and ensure legal clarity.

Q: Can enterprises use RodNIX without opening their source code?
A: Yes — when they hold an Enterprise License.


