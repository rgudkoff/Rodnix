# x86_64 - Implementation for x86_64 architecture

Kernel implementation for Intel/AMD x86_64 processors (CISC architecture).

## Features

- **CISC architecture**: Complex Instruction Set Computer
- **64-bit mode**: Long Mode (x86-64)
- **4-level page structure**: PML4 → PDPT → PD → PT
- **Canonical addresses**: Kernel in high-half (0xFFFFFFFF80000000)

## Components

- `config.h` - x86_64 configuration
- `types.h` - x86_64 data types
- `interrupts.c` - interrupt handling
- `memory.c` - memory management
- `cpu.c` - CPU operations
- `boot.S` - boot code

## Registers

x86_64 uses 16 64-bit general purpose registers:
- rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp
- r8, r9, r10, r11, r12, r13, r14, r15

## Page Structure

```
PML4 (512 entries) → PDPT (512 entries) → PD (512 entries) → PT (512 entries)
```

Page sizes:
- 4KB (regular pages)
- 2MB (large pages)
- 1GB (huge pages)
