# RodNIX

RodNIX is a modern operating system kernel designed for industrial-grade
stability, determinism, and long-term maintainability. The project focuses on
clear subsystem boundaries, security-oriented design, and a modular architecture
that can evolve without cascading regressions.

## Product Goals

- Industrial-grade determinism and fail-fast behavior
- Clear ownership of memory, scheduling, and subsystem boundaries
- Secure-by-default interfaces and explicit contracts
- Modular services and drivers with versioned APIs
- Predictable boot and initialization phases
- Observability as a first-class requirement (logs, traces, metrics)

## Current State

RodNIX is under active development. The current kernel provides:

- Deterministic boot and higher-half mapping with early physmap
- Structured subsystem initialization and fail-fast diagnostics
- Physical memory management and paging groundwork
- Interrupts, timers, and preemptive scheduling (priority queues)
- VFS with RAMFS + initrd import (RDNX format)
- IPC core with ports and minimal IDL runtime
- Fabric framework for buses/devices/drivers/services
- Minimal POSIX syscall surface and per-task fd table
- Shell and input pipeline through InputCore/Fabric

## Architecture Overview

High-level layout (simplified):
- boot: early boot code (Multiboot2, 32-bit entry)
- kernel/core: architecture-independent interfaces
- kernel/common: shared kernel subsystems
- kernel/arch: architecture-specific implementations
- kernel/fabric: bus/device/driver/service framework
- kernel/input: InputCore (scancode to ASCII, buffering)
- kernel/interrupts: shared interrupt pieces
- drivers: Fabric drivers (e.g., HID keyboard)
- include: public kernel headers
- docs: design and migration documents

Detailed design notes:
- docs/README.md
- ARCHITECTURE.md
- 64BIT_MIGRATION.md
 - docs/ru/industrial_readiness.md (RU, criteria for industrial readiness)

## Transition To Fabric (Facts Only)

1. Device/bus/driver/service routing is implemented in kernel/fabric.
2. Fabric is initialized in kernel/main.c before the shell starts.
3. Fabric buses registered: virtual bus, PCI, and PS/2 (kernel/fabric/bus).
4. The HID keyboard driver is a Fabric driver and publishes a keyboard service.
5. Legacy device manager (kernel/common/device.*) was removed in favor of Fabric.
6. Old arch-specific PS/2 keyboard driver was removed; input now flows through
   InputCore and the Fabric HID driver.

## Build Requirements

Toolchain:
- x86_64-elf-gcc (or i686 cross-compiler for early stages)
- nasm
- ld (binutils)
- qemu-system-x86_64
- xorriso
- mtools
- grub-mkrescue (i686-elf-grub-mkrescue on macOS/Homebrew)

## Keyboard and Shell

- PS/2 keyboard input with internal buffering
- Scancode-to-ASCII translation
- Modifier handling (Shift, Caps Lock)
- Simple interactive shell
- Line-based input with basic editing

The keyboard input path is currently polling-based through InputCore. IRQ-based
input is planned via Fabric.

## Roadmap

Near-term:
- Formalize error model and crash-dump format
- Expand observability (structured logs, tracepoints)
- Stabilize driver/service boundaries (versioned interfaces)
- Toolchain reproducibility and CI

Mid-term:
- Userspace boundary and stable syscall ABI
- Drivers and services isolation
- Network stack maturation (loopback → UDP → TCP)

Long-term:
- SMP and multi-core scheduling
- Capability-based security model
- Multi-architecture support

## Security Notice

RodNIX is security-oriented but still in active development.
Run in controlled environments and expect rapid iteration.
Report security issues via SECURITY.md.

## Industrial Readiness

RodNIX tracks industrial readiness criteria and gap analysis:

- docs/ru/industrial_readiness.md
- docs/ru/industrial_gap.md

## Contributing

Contributions are welcome.

Guidelines:
- Keep changes focused and well-documented
- Prefer clarity over cleverness
- Avoid unnecessary abstractions
- Discuss large changes before implementation

Final design decisions remain with the project maintainer.

## Guardrails

Run scripts/check_parallel_subsystems.sh to prevent reintroducing legacy
parallel subsystems (device manager, arch keyboard driver, interrupt stub).

## License

See LICENSE for open-source terms.

Commercial or enterprise usage may require a separate agreement.
See ENTERPRISE_LICENSE.md for details.
