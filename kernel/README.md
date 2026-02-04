# kernel - RodNIX Core Kernel

The kernel directory contains the core operating system kernel.

## Structure

- core: architecture-independent abstractions
- common: common kernel components
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
