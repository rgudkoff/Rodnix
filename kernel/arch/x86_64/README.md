# x86_64 - Implementation for x86_64 architecture

Kernel implementation for Intel/AMD x86_64 processors (CISC architecture).

## Features

- CISC architecture
- 64-bit mode (long mode)
- 4-level page structure (PML4, PDPT, PD, PT)
- Canonical addresses (kernel high-half at 0xFFFFFFFF80000000)

## Components

- config.h: x86_64 configuration
- types.h: x86_64 data types
- interrupts.c: interrupt handling
- memory.c: memory management
- cpu.c: CPU operations
- boot.S: boot code

## Registers

x86_64 uses 16 64-bit general purpose registers:
- rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp
- r8, r9, r10, r11, r12, r13, r14, r15

## Page Structure

- PML4, PDPT, PD, PT (512 entries each)

Page sizes:
- 4KB (regular pages)
- 2MB (large pages)
- 1GB (huge pages)
