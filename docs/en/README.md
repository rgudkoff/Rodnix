# RodNIX Documentation (EN)

This is an English mirror index for the active documentation set.
Source documents currently live in `docs/ru/`; this index provides EN labels
and direct links to those files.

## Start

- [`../ru/overview.md`](../ru/overview.md) - project scope and principles.
- [`../ru/architecture.md`](../ru/architecture.md) - current architecture and subsystem map.
- [`../ru/build_run.md`](../ru/build_run.md) - practical build and run guide.
- [`../ru/execution_plan_os_foundation.md`](../ru/execution_plan_os_foundation.md) - main executable delivery plan.
- [`../ru/p0_focus_plan.md`](../ru/p0_focus_plan.md) - current stabilization focus.

## Core Documents

- [`../ru/adr_darwin_layering.md`](../ru/adr_darwin_layering.md) - target layering model
  (`kernel mechanisms -> unix layer -> posix ABI`).
- [`../ru/unix_layer_contract.md`](../ru/unix_layer_contract.md) - contract and boundaries for `kernel/unix`.
- [`../ru/unix_process_model.md`](../ru/unix_process_model.md) - `spawn/exec/wait/exit` model.
- [`../ru/unix_process_contract_tests.md`](../ru/unix_process_contract_tests.md) - invariant and test matrix (CT-xxx).
- [`../ru/contract_governance.md`](../ru/contract_governance.md) - CI-gate and CT contract evolution rules.
- [`../ru/bsd_import_plan.md`](../ru/bsd_import_plan.md) - BSD code integration strategy.
- [`../ru/bsd_posix_userland_plan.md`](../ru/bsd_posix_userland_plan.md) - POSIX userland plan over BSD ABI.
- [`../ru/boot.md`](../ru/boot.md) - boot flow.
- [`../ru/memory.md`](../ru/memory.md) - memory model and current limits.
- [`../ru/scheduler.md`](../ru/scheduler.md) - scheduler model.
- [`../ru/syscalls.md`](../ru/syscalls.md) - syscall ABI and rules.
- [`../ru/vfs.md`](../ru/vfs.md) - VFS and directory/FD semantics.
- [`../ru/userspace.md`](../ru/userspace.md) - userland bootstrap and runtime.
- [`../ru/debugging.md`](../ru/debugging.md) - debugging and diagnostics.
- [`../ru/conventions.md`](../ru/conventions.md) - engineering conventions.
- [`../ru/industrial_readiness.md`](../ru/industrial_readiness.md) - release readiness criteria.
- [`../ru/industrial_gap.md`](../ru/industrial_gap.md) - gap analysis against readiness criteria.
- [`../ru/failure_model.md`](../ru/failure_model.md) - fail-fast and error model.

## Archive

- [`../ru/archive/`](../ru/archive/) - legacy documents (not current specification).
- [`../archive/`](../archive/) - archived historical EN high-level plans.

## Related Documents (EN)

- [`../../README.md`](../../README.md)
- [`../../ARCHITECTURE.md`](../../ARCHITECTURE.md)
- [`../../64BIT_MIGRATION.md`](../../64BIT_MIGRATION.md)
- [`../../ROADMAP.md`](../../ROADMAP.md)
- [`../README.md`](../README.md)
