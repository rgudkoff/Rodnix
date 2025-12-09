# RodNIX Architecture

## Overview

RodNIX is a microkernel-based operating system with strict separation between kernel and userspace.

## Kernel (C11)

The kernel is written in **C11** with the following constraints:
- No `stdio.h` or standard library
- No dynamic memory allocation except for the kernel's own allocator
- Minimal dependencies

### Kernel Responsibilities

The kernel provides only core functionality:

1. **Scheduler** - Process scheduling and context switching
2. **Virtual Memory (VM)** - Memory management, paging, address space isolation
3. **IPC** - Inter-process communication
4. **Capabilities** - Capability-based security
5. **IRQ Routing** - Interrupt routing and handling
6. **Bus Enumeration** - PCI/ACPI enumeration
7. **Memory Protection** - W^X, NX bit, KPTI-like address space separation

### Security Features

- **W^X (Write XOR Execute)**: Pages cannot be both writable and executable
- **NX Bit**: No-execute bit for data pages
- **KPTI-like separation**: Kernel and user address spaces are separated
- **copy_to_user/copy_from_user**: Safe copying between kernel and userspace

## Userspace (Rust 1.75+)

Userspace components are written in **Rust**:

### Drivers (no_std)

- Located in `userspace/drivers/`
- Compiled as static libraries or shared objects
- Use `no_std` for minimal runtime
- Communicate with kernel via syscalls

### Daemons (std)

- Located in `userspace/daemons/`
- Use standard Rust library
- Examples:
  - **device-manager**: Manages device registration and discovery
  - Other system daemons

### Library

- Common library in `userspace/lib/`
- Provides syscall interfaces and types
- Shared between drivers and daemons

## Directory Structure

```
RodNIX/
├── kernel/          # Kernel code (C11)
├── boot/            # Bootloader code
├── include/          # Kernel headers
├── drivers/          # Legacy drivers (to be migrated)
├── userspace/
│   ├── drivers/      # Rust drivers (no_std)
│   ├── daemons/     # Rust daemons (std)
│   └── lib/         # Common userspace library
└── build/            # Build artifacts
```

## Communication

- **Kernel ↔ Userspace**: Syscalls (int 0x80)
- **Process ↔ Process**: IPC messages
- **Driver ↔ Kernel**: Syscalls for device operations
- **Daemon ↔ Kernel**: Syscalls for system operations

## Memory Layout

- **0x00000000 - 0xBFFFFFFF**: Userspace (3GB)
- **0xC0000000 - 0xFFFFFFFF**: Kernel space (1GB)

## Build System

- **Kernel**: Makefile with i686-elf-gcc
- **Userspace**: Cargo workspace with Rust toolchain

## Future Work

- [ ] Implement full scheduler
- [ ] Implement IPC system
- [ ] Implement capability system
- [ ] Implement PCI enumeration
- [ ] Migrate existing drivers to Rust
- [ ] Implement device-manager daemon
- [ ] Add KPTI-like address space separation
- [ ] Add syscall handler

