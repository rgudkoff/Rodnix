# RodNIX
[![Discord](https://img.shields.io/discord/1483081290512334943?label=RodNIX%20Community&logo=discord&color=5865F2)](https://discord.gg/4sUPYXFB)

RodNIX is what Unix might look like if it were designed from scratch today.

RodNIX is an independent 64-bit operating system project built around that
idea: explicit kernel architecture, clean subsystem boundaries, observability,
and controlled ABI evolution from the start.

The current primary target is `x86_64` under `QEMU`.

## What Is In This Tree

RodNIX already contains working implementations of:

- Multiboot2-based `x86_64` boot
- physical and virtual memory management
- `mmap`, `munmap`, and `brk`
- interrupts, LAPIC timer, and baseline IRQ routing
- a preemptive scheduler
- VFS, `initrd`, EXT2, and the write path
- the Fabric device / driver / service model
- userspace bootstrap, a kernel shell, and userland utilities
- `fork`, `exec`, `wait`, signals, `poll`, `select`, and futex

The repository is still under active development. `QEMU_SMP=1` remains the
default and safest execution mode.

## Building

Basic build:

```bash
make clean
make
```

Build a bootable ISO image:

```bash
make iso
```

Run under QEMU:

```bash
make run
```

Run with verbose diagnostics:

```bash
make run-verbose
```

Override the virtual CPU model or vCPU count:

```bash
make run QEMU_CPU=max QEMU_SMP=2
```

## Toolchain Notes

The default build expects a cross-toolchain such as `x86_64-elf-gcc`,
`x86_64-elf-ld`, and the boot image tooling required by the project
configuration.

At this stage:

- `x86_64` is the actively exercised architecture
- `QEMU_SMP=1` is the recommended default
- higher SMP counts are useful for bring-up and inspection, but should still
  be treated as experimental

## Running RodNIX

Useful commands inside the system include:

- `hostinfo`
- `sysinfo`
- `cpuinfo`
- `diskinfo`
- `hwlist`
- `fabricls`
- `fabricevents`
- `scstat -a`
- `fsapitest`
- `syscalltest`
- `sigtest`

## Top-Level Layout

The tree is organized by subsystem, in a style closer to a conventional kernel
repository layout:

- `boot/` — early boot and bootloader integration
- `console/` — console backends and terminal-facing output paths
- `docs/` — active documentation and archives
- `drivers/` — hardware drivers
- `fs/` — VFS, devfs, EXT2, and filesystem support
- `idl/` — in-kernel IDL / IPC helpers and demos
- `include/` — shared public headers
- `init/` — staged kernel initialization and runtime handoff
- `kernel/` — core kernel runtime and architecture-dependent code
- `lib/` — shared kernel support code
- `mm/` — virtual memory and pager code
- `net/` — networking stack
- `sched/` — scheduler and wait queue code
- `scripts/` — build and developer tooling
- `shell/` — kernel shell
- `trace/` — boot tracing, logging, and observability
- `userland/` — userland programs and runtime support

Local build products are generated under `build/` and `iso/`.

## Documentation

Documentation is part of the project, not an afterthought.

Primary entry points:

1. `docs/README.md`
2. `docs/en/README.md`
3. `docs/ru/README.md`
4. `docs/ru/overview.md`
5. `docs/ru/architecture.md`
6. `docs/ru/build_run.md`

Language split:

- repository root documents are in English
- `docs/ru/` contains the active Russian documentation set
- `docs/en/` contains English documentation and navigation mirrors
- archived material stays under `docs/archive/` and `docs/ru/archive/`

## Contributing

See `CONTRIBUTING.md`.

Minimum baseline before submitting changes:

```bash
make
make -C userland
```

If behavior changes, also run the relevant smoke or contract checks from
`scripts/ci/` when applicable.

## Security

See `SECURITY.md` for vulnerability reporting.

## License

See:

- `LICENSE`
- `ENTERPRISE_LICENSE.md`
- `THIRD_PARTY_NOTICES.md`
