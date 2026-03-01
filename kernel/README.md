# kernel - RodNIX Core Kernel

The kernel directory contains the core operating system kernel.

## Structure

- core: architecture-independent abstractions
- common: common kernel components
- kern (planned): process/task/scheduler/syscall/ipc/security domain
- vm (planned): memory management policy/helpers (arch-independent)
- arch: architecture-dependent implementations
  - x86_64: x86_64 (CISC)
  - arm64: ARM64 (RISC)
  - riscv64: RISC-V64 (RISC)
- fabric: Fabric core (bus/device/driver/service)
- input: InputCore

## Principles

1. Architectural abstraction: arch-specific details are isolated in arch/
2. Unified interface: core provides interfaces for all architectures
3. Modularity: components are separated and replaceable
4. Domain-first layout: avoid placing new process/vm logic into common/

## Refactor plan

See `/Users/romangudkov/dev/Rodnix/docs/ru/kernel_layout_refactor.md` for the
XNU-inspired file layout migration plan.

## Components

core:
- arch_types.h: basic data types
- config.h: kernel configuration
- interfaces for interrupts, memory, CPU, etc.

common:
- scheduler
- memory management
- IPC
- shell and console

arch:
- each architecture implements core interfaces
- arch-specific optimizations
