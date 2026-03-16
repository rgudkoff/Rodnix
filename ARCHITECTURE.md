# RodNIX Architecture

RodNIX is what Unix might look like if it were designed from scratch today.

Architecturally, that means a 64-bit operating system project built around
explicit subsystem boundaries, observable behavior, and controlled ABI
evolution. The working implementation is focused on `x86_64` today, with
placeholders for other 64-bit ISAs.

## Core Principles

1. 64-bit only
   The codebase targets 64-bit CPUs only to simplify design and use modern
   CPU features.

2. Explicit subsystem boundaries
   The tree is organized by domain rather than by a single monolithic kernel
   bucket. Memory management, scheduling, filesystems, tracing, networking,
   shell, and console paths live in separate top-level subsystems.

3. Architecture abstraction
   The runtime is split between architecture-independent interfaces and
   architecture-specific implementations:
   - `kernel/core`: arch-independent low-level interfaces
   - `kernel/arch`: arch-specific implementations

4. Multiple ISA placeholders
   The tree contains placeholders for CISC (`x86_64`) and RISC (`arm64`,
   `riscv64`), but `x86_64` is the only actively working target.

5. Fabric (bus / device / driver / service)
   A small Fabric layer manages bus registration, device publication,
   driver matching, and service publication.

## Project Layout

- `boot/`: early boot and bootloader integration
- `console/`: console backends and terminal-facing output paths
- `drivers/`: hardware drivers
- `fs/`: VFS, devfs, EXT2, and filesystem support
- `idl/`: in-kernel IDL / IPC helpers and demos
- `init/`: staged kernel initialization and runtime handoff
- `kernel/`: core runtime, low-level internals, and architecture code
- `lib/`: shared kernel support code
- `mm/`: virtual memory and pager code
- `net/`: networking stack
- `sched/`: scheduler and wait queue code
- `shell/`: kernel shell
- `trace/`: boot tracing, logging, and observability
- `userland/`: userland programs and runtime support

Key subareas inside `kernel/`:

- `kernel/core`: low-level architecture-independent interfaces
- `kernel/arch`: architecture-specific implementations
- `kernel/fabric`: Fabric core
- `kernel/input`: input core
- `kernel/posix`, `kernel/unix`: syscall and user ABI layers

## Architecture Differences

`x86_64` (CISC)
- Complex instruction set
- Variable instruction length
- Multiple addressing modes
- 4-level page tables (PML4)

`ARM64` (RISC)
- Fixed-length instructions
- Load/store architecture
- 4-level page tables
- Exception Levels (EL)

`RISC-V64` (RISC)
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
3. Testability: domain boundaries stay visible
4. Maintainability: changes in one area are less likely to leak into others

## Current Status

- [x] Base directory structure
- [x] x86_64 base components (boot, IDT/IRQ, PIC/APIC, PIT, paging/PMM)
- [x] Task scheduler (preemptive, TIMESHARE/REALTIME, priority inheritance)
- [x] Memory management (PMM, VM map, vm_object, page fault, COW groundwork)
- [x] IPC subsystem (ports, queues, refcounted rights)
- [x] VFS + RAMFS/initrd + EXT2 read/write (direct + single + double indirect)
- [x] Device drivers via Fabric (HID keyboard, IDE storage, virtio-net stub)
- [x] POSIX syscall surface (fork, exec, wait, signals, poll, futex, pipes)
- [x] Userland init and shell with utilities
- [ ] ARM64 base components
- [ ] RISC-V64 base components
- [x] IRQ-based input path through Fabric (IRQ1, polling fallback for disabled-irq paths)
- [x] Hierarchical scheduler v1 (4 QoS buckets, per-bucket quantum, starvation avoidance, thread_group CPU accounting)
- [ ] Network: TCP stack + real virtio-net (UDP/ICMP loopback done)

For the repository entry point and practical build/run instructions, start with
`README.md` and `INSTALL.md`.
