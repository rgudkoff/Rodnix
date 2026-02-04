# RodNIX

RodNIX is an experimental operating system kernel targeting x86_64 today, with
placeholders for other 64-bit ISAs. The project focuses on clarity,
security-oriented design, and gradual evolution toward a modern, modular OS
architecture (drivers and services layered over a simple hardware fabric).

RodNIX is developed as a learning and research platform with engineering
discipline and long-term extensibility in mind.

## Project Goals

- Build a clean and understandable kernel without legacy constraints
- Use x86_64 as the primary target, with a Multiboot2 32-bit entry stub
- Separate architecture-independent core from arch-specific code
- Gradually move drivers out of the kernel to reduce attack surface
- Build a Fabric layer for buses/devices/drivers/services
- Favor explicit design over hidden magic
- Provide a foundation for future experimentation (SMP, userspace, microkernel)

## Current State

RodNIX is in early active development.

Implemented so far (x86_64 focus):
- Multiboot2-compliant boot sequence
- 32-bit boot stub with transition toward 64-bit long mode
- GDT and IDT initialization
- ISR and IRQ handling
- PIC remapping
- PIT timer initialization
- VGA text-mode console
- Minimal interactive shell
- PS/2 keyboard input (polling fallback via InputCore; IRQ path prepared)
- Physical memory manager (bitmap, early fixed map) and paging groundwork
- Fabric core: bus/device/driver/service registries and matching
- Buses: virtual bus, PCI enumeration (minimal), PS/2 keyboard publication
- HID keyboard driver (via Fabric) publishing a "keyboard" service
- ISO image generation and QEMU support

Expect frequent changes and refactoring.

## Architecture Overview

High-level layout (simplified):
- boot: early boot code (Multiboot2, 32-bit entry)
- kernel/core: architecture-independent interfaces
- kernel/common: shared kernel subsystems
- kernel/arch/x86_64: x86_64 implementation
- kernel/fabric: bus/device/driver/service framework
- kernel/input: InputCore (scancode to ASCII, buffering)
- kernel/interrupts: shared interrupt pieces
- drivers: Fabric drivers (e.g., HID keyboard)
- include: public kernel headers
- docs: design and migration documents

Detailed design notes:
- ARCHITECTURE.md
- 64BIT_MIGRATION.md

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
- grub-mkrescue

## Keyboard and Shell

- PS/2 keyboard input with internal buffering
- Scancode-to-ASCII translation
- Modifier handling (Shift, Caps Lock)
- Simple interactive shell
- Line-based input with basic editing

The keyboard logic is currently low-level and architecture-specific by design.

## Roadmap

Short-term:
- Complete x86_64 long mode transition
- Stable paging with higher-half kernel mapping
- Cleanup of interrupt and timer subsystems
- Improved shell commands
- Integrate IRQ-based keyboard path through Fabric (replace polling fallback)

Mid-term:
- Driver isolation and abstraction layer
- Expand Fabric services and consumers (service lookup usage)
- Basic VFS and RAM-backed filesystem
- Userspace boundary definition
- Syscall interface draft

Long-term:
- SMP support
- APIC support
- ELF userspace loader
- Security hardening and capability model

## Security Notice

RodNIX is experimental kernel code.

Do not run on production systems.
Do not test on machines with sensitive data.
Expect crashes, hangs, and undefined behavior.

Security issues can be reported via SECURITY.md.

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
