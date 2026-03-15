# RodNIX Architecture

RodNIX is an experimental 64-bit OS kernel. The working implementation is
focused on x86_64 today, with placeholders for other 64-bit ISAs.

## Core Principles

1. 64-bit only
   The codebase targets 64-bit CPUs only to simplify design and use modern
   CPU features.

2. Architecture abstraction
   The kernel is split into architecture-independent interfaces and
   architecture-specific implementations:
   - kernel/core: arch-independent interfaces
   - kernel/arch: arch-specific implementations

3. Multiple ISA placeholders
   The tree contains placeholders for CISC (x86_64) and RISC (ARM64, RISC-V64),
   but x86_64 is the only actively working target.

4. Fabric (bus/device/driver/service)
   A small Fabric layer manages bus registration, device publication,
   driver matching, and service publication.

## Project Layout (simplified)

- kernel/core: arch-independent interfaces
- kernel/common: shared kernel subsystems
- kernel/arch: architecture-specific implementations
  - x86_64: active implementation
  - arm64: placeholder
  - riscv64: placeholder
- kernel/fabric: Fabric core (bus/device/driver/service)
- kernel/input: InputCore (scancode to ASCII)
- input path: polling-based today, IRQ path planned via Fabric
- kernel/interrupts: shared interrupt helpers
- boot: boot code
- drivers: Fabric drivers
- include: public headers
- docs: design and migration documents

## Architecture Differences

x86_64 (CISC)
- Complex instruction set
- Variable instruction length
- Multiple addressing modes
- 4-level page tables (PML4)

ARM64 (RISC)
- Fixed-length instructions
- Load/store architecture
- 4-level page tables
- Exception Levels (EL)

RISC-V64 (RISC)
- Minimal instruction set
- Modular ISA extensions
- Page tables: Sv39/Sv48/Sv57
- Privilege levels: User, Supervisor, Machine

## Abstractions

Interrupts
- Handler registration
- IRQL management
- IPI (Inter-Processor Interrupts)

Memory
- Page mapping
- Allocation and free
- Address translation
- Multiboot2 memory map parsing at boot

CPU
- CPU info
- Context switching
- Atomic ops
- Memory barriers

## Benefits

1. Portability: easier to add a new architecture
2. Modularity: clear separation of responsibilities
3. Testability: arch-independent code is easier to test
4. Maintainability: changes in one arch do not spill into others

## Current Status

- [x] Base directory structure
- [x] x86_64 base components (boot, IDT/IRQ, PIC/APIC, PIT, paging/PMM)
- [x] Task scheduler (preemptive, TIMESHARE/REALTIME, priority inheritance)
- [x] Memory management (PMM, VM map, vm_object, page fault, COW groundwork)
- [x] IPC subsystem (ports, queues, refcounted rights)
- [x] VFS + RAMFS/initrd + EXT2 read/write (direct + single indirect)
- [x] Device drivers via Fabric (HID keyboard, IDE storage, virtio-net stub)
- [x] POSIX syscall surface (fork, exec, wait, signals, poll, futex, pipes)
- [x] Userland init and shell with utilities
- [ ] ARM64 base components
- [ ] RISC-V64 base components
- [x] IRQ-based input path through Fabric (IRQ1, polling fallback for disabled-irq paths)
- [ ] Hierarchical scheduler (bucket → group → thread)
- [ ] Network: TCP stack + real virtio-net (UDP/ICMP loopback done)
