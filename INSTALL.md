# Installation and Run Guide

## Purpose

This document describes the minimum environment required to build and run
RodNIX and provides practical commands for local development.

## Requirements

The following tools are required:

- `x86_64-elf-gcc`
- `x86_64-elf-ld`
- `nasm`
- `qemu-system-x86_64`
- `grub-mkrescue` or an available ISO creation fallback
- `python3`

Environment check:

```bash
make check-deps
```

## Recommended Setup

RodNIX is built with a cross-toolchain and runs in `QEMU`. The exact way these
tools are installed depends on the host system, but the required tool set is
always the same:

1. install the `x86_64-elf-*` cross-toolchain;
2. install `nasm`;
3. install `qemu-system-x86_64`;
4. ensure an ISO creation tool path is available;
5. validate the setup with `make check-deps`.

## Build

Full build:

```bash
make clean
make
make iso
```

Build userland only:

```bash
make -C userland
```

## Run

Normal run:

```bash
make run
```

Verbose run:

```bash
make run-verbose
```

Run with an alternative CPU model:

```bash
make run QEMU_CPU=max
```

Run with multiple virtual CPUs:

```bash
make run QEMU_SMP=2
```

Run with both multiple vCPUs and a richer CPU model:

```bash
make run QEMU_CPU=max QEMU_SMP=2
```

Important:

- `QEMU_SMP=1` remains the safe default mode;
- `QEMU_SMP>1` is useful for CPU topology inspection and SMP preparation, but it
  should not yet be treated as fully supported kernel operation.

## Debugging

Run under a debugger:

```bash
make gdb
```

Additional references:

- `docs/ru/debugging.md`
- `docs/ru/build_run.md`

## Post-Boot Checks

Recommended commands inside RodNIX:

```text
hostinfo
sysinfo
cpuinfo
hwlist
fabricls
diskinfo
scstat -a
```

## Common Problems

### `qemu-system-x86_64` is missing

- install `QEMU` using your host package manager;
- rerun `make check-deps`.

### Cross-toolchain is missing

- install `x86_64-elf-gcc` and matching binutils;
- verify that the binaries are visible in `PATH`.

### ISO creation fails

- verify that `grub-mkrescue` is available;
- if a fallback path is expected, ensure the required supporting tools are
  installed in the local environment.

### System becomes unstable with `QEMU_SMP>1`

- this is expected until full SMP bring-up is complete;
- retry with `QEMU_SMP=1` and use `cpuinfo` only to inspect the topology
  advertised by QEMU.
