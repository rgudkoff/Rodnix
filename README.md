# RodNIX

RodNIX is what Unix might look like if it were designed from scratch today.

RodNIX is an independent 64-bit operating system project focused on explicit
kernel architecture, subsystem observability, and controlled ABI evolution.
The primary target platform at this stage is `x86_64` running under `QEMU`.

The project is guided by the following principles:

- explicit subsystem boundaries;
- predictable debugging and reproducibility;
- incremental POSIX-compatible userland growth;
- minimal historical complexity carried forward by default;
- engineering documentation treated as part of the product.

## Current Status

The repository already includes working implementations of:

- `x86_64` boot via Multiboot2;
- baseline memory management: PMM, VM, `mmap`, `munmap`, `brk`;
- interrupts, LAPIC timer, and baseline IRQ routing;
- a preemptive scheduler;
- VFS, `initrd`, EXT2 mount, and the write path;
- the Fabric model for devices, drivers, and services;
- userland bootstrap, shell, and diagnostic utilities;
- `fork`, `exec`, `wait`, signals, `poll`, `select`, and futex;
- system utilities such as `hostinfo`, `sysinfo`, `cpuinfo`, `diskinfo`,
  `hwlist`, `fabricls`, `fabricevents`, and `scstat`.

## Quick Start

Basic build and run:

```bash
make clean
make
make iso
make run
```

Run with verbose diagnostics:

```bash
make run-verbose
```

Run with an overridden CPU model and vCPU count:

```bash
make run QEMU_CPU=max QEMU_SMP=2
```

Important:

- `QEMU_SMP=1` remains the safe default;
- `QEMU_SMP>1` is useful for CPU topology inspection and SMP bring-up work,
  but it should not yet be treated as the kernel production default.

## Useful Commands Inside RodNIX

- `hostinfo` — compact system summary;
- `sysinfo` — extended kernel/system report;
- `cpuinfo` — detailed CPU topology and feature report;
- `hwlist` — discovered hardware inventory;
- `fabricls` — Fabric object listing;
- `fabricevents` — system event stream;
- `diskinfo` — block device diagnostics;
- `scstat -a` — syscall path statistics;
- `fsapitest`, `syscalltest`, `sigtest` — targeted userland test tools.

## Repository Layout

- `boot/` — early boot and bootloader integration;
- `kernel/` — kernel core, architecture code, and subsystems;
- `drivers/` — drivers and Fabric-facing integration;
- `include/` — shared headers used by kernel and userland;
- `userland/` — shell, utilities, and runtime support;
- `scripts/` — build, CI, and developer tooling;
- `docs/` — active documentation plus archive;
- `third_party/` — imported external material;
- `build/`, `iso/` — local build artifacts.

## Documentation

The documentation language split is intentional:

- top-level documents in the repository root are in English;
- `docs/ru/` contains the Russian active documentation set;
- `docs/en/` contains English documentation and navigation mirrors;
- archived materials stay under `docs/archive/` and `docs/ru/archive/`.

Recommended starting points:

1. `docs/README.md`
2. `docs/en/README.md`
3. `docs/ru/README.md`
4. `docs/ru/overview.md`
5. `docs/ru/architecture.md`
6. `docs/ru/build_run.md`

## Contributing

Contribution rules are documented in `CONTRIBUTING.md`.

Minimum baseline before submitting changes:

```bash
make
make -C userland
```

For behavior changes, also run the relevant smoke or contract checks from
`scripts/ci/`.

## Security

The vulnerability reporting process is described in `SECURITY.md`.

## License

See:

- `LICENSE`
- `ENTERPRISE_LICENSE.md`
- `THIRD_PARTY_NOTICES.md`
