# ARM64 - Implementation for ARM64 architecture

Kernel implementation for ARM 64-bit processors (RISC architecture).

## Features

- **RISC architecture**: Reduced Instruction Set Computer
- **64-bit mode**: AArch64
- **4-level page structure**: Level 0 → Level 1 → Level 2 → Level 3
- **EL (Exception Levels)**: Privilege levels

## Components

- `config.h` - ARM64 configuration
- `types.h` - ARM64 data types
- `interrupts.c` - interrupt handling (IRQ/FIQ)
- `memory.c` - memory management (MMU)
- `cpu.c` - CPU operations
- `boot.S` - boot code

## Registers

ARM64 uses 31 64-bit general purpose registers:
- x0-x30 (x30 = LR, Link Register)
- SP (Stack Pointer)
- PC (Program Counter)

## Page Structure

```
Level 0 (512 entries) → Level 1 (512 entries) → Level 2 (512 entries) → Level 3 (512 entries)
```

Page sizes:
- 4KB (regular pages)
- 2MB (large pages)
- 1GB (huge pages)

## Exception Levels

- EL0: User mode
- EL1: Kernel mode
- EL2: Hypervisor
- EL3: Secure monitor
