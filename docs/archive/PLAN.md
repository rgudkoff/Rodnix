# RodNIX Kernel Plan (Working Draft)

This plan focuses on turning the current kernel into a minimal but serious
foundation. The steps are ordered by impact and dependency.

## Phase 0: Preemptive Scheduler Stabilization

Goal: reliable timer-driven preemption and clean IRQ context switching.

1. Verify timer IRQ (vector 32) consistently fires and calls scheduler_tick()
2. Ensure scheduler_switch_from_irq() only switches on resched_pending (except first switch)
3. Confirm thread start path (interrupt_frame_t) is correct and iretq returns to thread_trampoline
4. Remove temporary diagnostics after confirming shell reliably starts
5. Fix duplicate scheduler_tick() on IRQ32 (ensure single tick per interrupt)

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
4. Priority scheduling + time slice (MLQ/priority queues)

## Phase 4: VFS + RAMFS

Goal: basic filesystem and shell integration.

1. VFS interfaces
2. RAM-backed filesystem
3. Shell commands: ls, cat, echo > file
4. vnode/inode model
5. Name cache
6. Mounting + initrd/ramfs for boot

## Phase 5: Userspace Boundary

Goal: minimal ring3 and syscall boundary.

1. Syscall table
2. CPL3 transition
3. Minimal user program loader (ELF later)
4. Bootstrap port + bootstrap server (service port registry)
5. IPC: fixed/variable messages + port transfer
6. Syscall table in BSD layer, traps from userland
7. UID/GID (real/effective) + basic permissions (MAC hooks later)

## Phase 6: Documentation Sync (RU → EN)

Goal: keep implementation and docs aligned and guide development.

1. Consolidate kernel architecture doc (RU) in `docs/ru/`
2. Capture strategic design points (modular monolith, in-kernel subsystems, IPC/IDL)
3. Reconcile docs vs implementation and write diffs to a dedicated file
4. Duplicate RU docs to EN once RU baseline is stable

## Phase 7: Debugging and Testing

Goal: predictable diagnostics and regression checks.

1. panic() with crash info
2. Serial logger (not VGA-only)
3. Small unit tests for core helpers

## Phase 8: IPC + IDL

Goal: enforce layer boundaries and avoid manual marshalling.

1. IDL (.defs) format
2. Code generator for client/server stubs and dispatchers
3. Enforce IDL at inter-layer boundaries

## Phase 9: Networking

Goal: BSD sockets with minimal stack.

1. AF_INET sockets
2. Loopback
3. UDP
4. TCP

## Immediate Next Steps

1. Stabilize timer IRQ preemption (Phase 0 items 1–3)
2. Confirm shell startup is reliable, then remove temporary diagnostics
3. Start RU architecture doc baseline in `docs/ru/`
4. Implement Multiboot2 memory map parsing
5. Replace fixed PMM bounds with real regions
6. Wire IRQ keyboard path through Fabric
