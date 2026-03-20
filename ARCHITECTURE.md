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

## Subsystem Design Decisions

### Network Stack

The network stack lives in the kernel (`net/`), not in userland. This is the
right architecture for RodNIX today and the foreseeable future:

- **Kernel-resident**: the stack runs in kernel space alongside the scheduler,
  VM, and VFS. Sockets are kernel objects; data paths avoid costly privilege
  crossings.

- **Modular inside the kernel**: protocol layers (Ethernet, IP, TCP, UDP, ICMP)
  are implemented as discrete, replaceable units. The BSD `mbuf`/`ifnet`/`inet`
  model provides proven internal interfaces without locking the design to any
  specific userland ABI.

- **Not a microkernel network server**: splitting the stack into a userland
  process would add IPC overhead on every packet and complicate the memory
  model for zero-copy paths. That trade-off is not worth it for a
  single-machine OS without a strong isolation requirement for the network
  server itself.

- **Migration path preserved**: the internal layer boundaries are kept clean so
  that individual protocol handlers could later be replicated in userland (e.g.
  for sandboxed protocol parsers or userland QUIC) or moved to an isolated
  kernel module without redesigning the socket API.

### Rust in the Network Stack

The network stack is one of the best candidates for introducing Rust into the
kernel, but selectively — not as a wholesale replacement of C.

**Good targets for Rust:**

- **Packet parsing** — IP, TCP, UDP, ICMP header parsing. Rust's type system
  and exhaustive pattern matching eliminate a class of off-by-one and
  bounds-violation bugs that are common in hand-written C parsers. The parsing
  layer has no interrupt context, no special calling convention, and a clear
  input/output contract — a clean fit for a `no_std` Rust module.

- **Checksums** — IP and TCP checksum computation is pure arithmetic over a
  byte slice. Rust gives correctness guarantees for free and compiles to the
  same code as C.

- **TCP state machine** — the FSM logic (SYN/SYN-ACK/ACK/FIN transitions,
  timer management, retransmit logic) is complex, stateful, and historically
  buggy. Encoding it in Rust with explicit state enums and exhaustive match
  turns illegal state transitions into compile-time errors.

**What stays in C:**

- **Interrupt path and driver glue** — the virtio-net and e1000 drivers fire
  from interrupt context, interact with MMIO/PCI directly, and must conform
  to Fabric driver ABI. Keep these in C.

- **mbuf / ifnet integration** — the core buffer management and interface layer
  are tightly coupled to the kernel memory allocator and scheduler. Rewriting
  this boundary in Rust would require deep FFI plumbing with no safety gain.

**Integration model:**

Rust modules expose a plain C ABI (`extern "C"`). The C network core calls
into Rust for parsing and state-machine work; Rust never calls back into C
except through explicit, documented FFI boundaries. This keeps the `unsafe`
surface small and auditable.

## Language Policy

Each language is used where it provides a clear advantage:

- **C** — primary language for the kernel and base userland. Direct hardware
  access, predictable ABI, and zero runtime overhead make it the right tool
  for anything that runs in privileged mode or forms the core userland runtime.

- **Assembly** — low-level entry points and CPU glue only: boot stubs,
  interrupt entry/exit, context switch, syscall fast path. Kept to the
  absolute minimum; the rest is C.

- **Rust** — safe userland utilities, parsers, and protocol handlers where
  memory safety eliminates a class of bugs with no kernel-ABI cost. Longer
  term: isolated kernel modules and drivers where the sandbox boundary is
  well-defined.

- **Python** — host tooling: build scripts, test harnesses, image generation,
  CI helpers. Not used in the kernel or on-target userland.

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
