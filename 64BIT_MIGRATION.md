# 64-bit Migration Notes

This document summarizes the high-level changes required when moving from
32-bit (i386) to 64-bit (x86_64).

## Build Toolchain

- Use x86_64-elf-gcc and x86_64-elf-ld
- Use -m64, -mcmodel=kernel, -mno-red-zone
- Use ELF64 output format in link.ld

## Data Types

- Pointers become 64-bit
- uintptr_t and intptr_t must be 64-bit
- size_t is typically 64-bit in long mode

## Paging

32-bit:
- 2-level page tables (PD -> PT)
- 32-bit entries

64-bit:
- 4-level page tables (PML4 -> PDPT -> PD -> PT)
- 64-bit entries
- Support for 4KB, 2MB, and 1GB pages
- NX (no-execute) bit available

## CPU Registers

- General registers: rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8-r15
- Control registers: cr0, cr3, cr4, cr8

## ABI and Assembly Updates

- Use 64-bit operands and registers in inline asm
- Make sure stack alignment matches the x86_64 ABI
- CR3 points to the PML4 base (not a page directory)

## Linking and Addressing

- Kernel can be linked in the higher half (e.g. 0xFFFFFFFF80000000)
- Identity-map early boot memory as needed

This is a high-level checklist; see code and docs for implementation details.
