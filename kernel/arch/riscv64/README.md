# RISC-V64 - Implementation for RISC-V64 architecture

Kernel implementation for RISC-V 64-bit processors (RISC architecture).

## Features

- **RISC architecture**: Reduced Instruction Set Computer
- **64-bit mode**: RV64
- **Page structure**: Sv39, Sv48, or Sv57
- **Privileges**: User, Supervisor, Machine

## Components

- `config.h` - RISC-V64 configuration
- `types.h` - RISC-V64 data types
- `interrupts.c` - interrupt handling
- `memory.c` - memory management (MMU)
- `cpu.c` - CPU operations
- `boot.S` - boot code

## Registers

RISC-V64 uses 32 64-bit general purpose registers:
- x0-x31 (x0 = zero register, always 0)
- SP (Stack Pointer, usually x2)
- PC (Program Counter)

## Page Structure

RISC-V supports several virtualization modes:
- **Sv39**: 39-bit virtual addresses (3 levels)
- **Sv48**: 48-bit virtual addresses (4 levels)
- **Sv57**: 57-bit virtual addresses (5 levels)

Page sizes:
- 4KB (regular pages)
- 2MB (megapages)
- 1GB (gigapages)

## Privileges

- User (U): 0
- Supervisor (S): 1
- Machine (M): 3
