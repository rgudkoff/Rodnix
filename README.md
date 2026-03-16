# RodNIX

RodNIX is an experimental x86_64 operating system kernel.
The project focuses on clean subsystem boundaries, predictable boot/runtime
behavior, and practical observability while the system is still small.

## What Works Today

- Boot on QEMU with Multiboot2 and higher-half kernel mapping
- Physical memory manager and basic VM path (`mmap/munmap/brk`, COW groundwork)
- Interrupts, timers, and preemptive scheduler
- VFS with RAMFS and initrd import
- Fabric device model (`device -> driver -> service`) with event stream
- Basic userland launch path (`/bin/init`, `/bin/sh`) and minimal POSIX syscalls
- Hardware/system introspection utilities in userland

## Repository Layout

- `boot/` - early boot code
- `kernel/` - core kernel code
- `drivers/` - hardware and Fabric drivers
- `userland/` - userspace binaries and headers
- `scripts/` - CI/smoke helpers
- `docs/` - active and archived documentation

## Build and Run

Minimum toolchain:
- `x86_64-elf-gcc`, `binutils`, `nasm`
- `qemu-system-x86_64`
- `xorriso`, `mtools`, `grub-mkrescue`

Typical flow:

```bash
make clean
make
make iso
make run
```

More setup details: `INSTALL.md` and `docs/ru/build_run.md`.

## Useful In-System Commands

- `sysinfo` - CPU, memory, uptime, interrupts, Fabric counters
- `hostinfo` - compact host snapshot (CPU/memory/Fabric/syscall counters)
- `hwlist` - discovered hardware list
- `fabricls` - Fabric topology snapshot
- `fabricevents` - Fabric event queue dump
- `fabricnetcheck` - net service lifecycle checks
- `scstat` - per-syscall counters (`int80` vs `fast`)
- `sleep <seconds>` - userspace delay via `nanosleep`
- `sigtest` - minimal signal path check (`sigaction/kill/sigreturn`)
- `stty` - inspect/change terminal mode (`raw/cooked`, echo/signals/control chars)
- shell supports `|`, `<`, `>`, `>>`, `2>`, `2>>`

## Documentation

- `docs/README.md` - docs index
- `docs/en/README.md` - English mirror index
- `docs/ru/README.md` - active documentation set
- `ARCHITECTURE.md` - architecture notes
- `ROADMAP.md` - roadmap overview

## Contributing

See `CONTRIBUTING.md` for commit/PR style, validation baseline, and review rules.

## License

See `LICENSE` and `ENTERPRISE_LICENSE.md`.
