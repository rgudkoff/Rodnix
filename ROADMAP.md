# RodNIX High-Level Roadmap

Last updated: 2026-03-08

This roadmap is intentionally high level. Detailed execution plans live in:
- `docs/ru/execution_plan_os_foundation.md`
- `docs/ru/industrial_gap.md`
- `docs/archive/posix-plan.md` (historical)

## Current baseline (already done)
- [x] x86_64 long mode and high-half kernel boot
- [x] Stable early paging + PMM groundwork
- [x] Preemptive scheduler baseline (timer-driven)
- [x] Fabric buses/devices/drivers baseline + PS/2 keyboard path
- [x] VFS + RAMFS/initrd (`RDNX`) boot path
- [x] Ring3 transition and userspace boot path (`run /bin/init`)
- [x] Minimal POSIX syscall path (`getpid/open/read/write/close/exit`)

## Phase 1 - Stabilization (P0)
- [ ] Finalize process/thread lifecycle teardown (no leaks/corruption)
- [ ] Freeze syscall namespace and ABI numbering policy
- [ ] Complete user pointer safety baseline in syscall handlers
- [ ] Add mandatory CI smoke: `boot -> /bin/init -> userspace shell`

## Phase 2 - OS Foundation (P1)
- [ ] POSIX Level-1 core: `openat/lseek/stat/fstat/chdir/getcwd/dup2/pipe/fcntl`
- [ ] Process model v1: `fork/execve/waitpid` (+ basic `SIGCHLD`)
- [ ] VM v1: explicit user VM map/object model + page-fault path + basic COW
- [ ] VFS semantics hardening for userland workflows

## Phase 3 - Reliability and Security (P2)
- [ ] Structured kernel logs + tracepoints (`irq/sched/syscall/fault`)
- [ ] Crash dump format (registers, backtrace, task context)
- [ ] Consistent UID/GID checks across syscall/VFS/IPC
- [ ] Trusted vs untrusted execution model and hardening baseline

## Phase 4 - Productization
- [ ] Reproducible toolchain versions and release build profile
- [ ] Automated regression/stress suite in CI (boot/process/fs/ipc/network)
- [ ] Release engineering: versioning, artifacts, rollback strategy
- [ ] Documentation set for operators/developers and support workflows

## Phase 5 - Market Release
- [ ] Release Candidate cycle with blocker-only policy
- [ ] Hardware support matrix for target deployment profile
- [ ] Security response process and supported-version policy
- [ ] RodNIX 1.0 GA for a constrained initial market segment
