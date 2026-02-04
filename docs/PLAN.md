# RodNIX Kernel Plan (Working Draft)

This plan focuses on turning the current kernel into a minimal but serious
foundation. The steps are ordered by impact and dependency.

## Phase 1: Memory Foundation

Goal: correct memory accounting and a safe allocation baseline.

1. Parse Multiboot2 memory map
   - Stop using fixed PMM bounds
   - Build a list of usable and reserved regions

2. PMM v2
   - Support regions and holes
   - Track low/normal/MMIO zones
   - Added free-range lists per zone and zone-aware allocation
   - Added PMM region snapshot API for VMM
   - Added usable/reserved region lists from MB2 map
   - Mark non-available MB2 ranges as MMIO

3. VMM baseline
   - Kernel heap (simple allocator)
   - Address space structure for kernel mappings
   - Temporary identity-mapped vmm_alloc/vmm_free backed by PMM
   - Basic kernel heap (kmalloc/kfree) built on VMM

## Phase 2: Interrupts and Timers

Goal: stable IRQ path without hacks.

DONE: Remove diagnostic VGA spam from IRQ paths (keep optional debug mode)
DONE: Normalize PIC/APIC EOI logic
DONE: Route keyboard IRQ through Fabric
   - Use fabric_request_irq
   - Keep polling only as fallback

## Phase 3: Scheduler (Minimal but Real)

Goal: preemptive scheduling with at least two runnable threads.

1. Timer tick drives scheduling
2. Simple runqueue (round-robin)
3. Context switch correctness and accounting

## Phase 4: VFS + RAMFS

Goal: basic filesystem and shell integration.

1. VFS interfaces
2. RAM-backed filesystem
3. Shell commands: ls, cat, echo > file

## Phase 5: Userspace Boundary

Goal: minimal ring3 and syscall boundary.

1. Syscall table
2. CPL3 transition
3. Minimal user program loader (ELF later)

## Phase 6: Debugging and Testing

Goal: predictable diagnostics and regression checks.

1. panic() with crash info
2. Serial logger (not VGA-only)
3. Small unit tests for core helpers

## Immediate Next Steps

1. Implement Multiboot2 memory map parsing
2. Replace fixed PMM bounds with real regions
3. Wire IRQ keyboard path through Fabric
