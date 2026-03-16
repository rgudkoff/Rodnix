# ARM64 - Implementation for ARM64 architecture

Kernel implementation for ARM 64-bit processors (RISC architecture).

## Features

- RISC architecture
- 64-bit mode (AArch64)
- 4-level page structure: Level 0 to Level 3
- Exception levels (EL)

## Components

- config.h: ARM64 configuration
- types.h: ARM64 data types
- interrupts.c: interrupt handling (IRQ/FIQ)
- memory.c: memory management (MMU)
- cpu.c: CPU operations
- boot.S: boot code

## Registers

ARM64 uses 31 64-bit general purpose registers:
- x0-x30 (x30 is LR)
- SP (stack pointer)
- PC (program counter)

## Page Structure

- Level 0, Level 1, Level 2, Level 3 (512 entries each)

Page sizes:
- 4KB (regular pages)
- 2MB (large pages)
- 1GB (huge pages)

## Exception Levels

- EL0: user mode
- EL1: kernel mode
- EL2: hypervisor
- EL3: secure monitor
