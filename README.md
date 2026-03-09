# RodNIX

RodNIX is what Unix might look like if it were designed from scratch today.

The project keeps the Unix model that still matters: small tools, clear
interfaces, process isolation, and a system you can understand end to end.
What it does not inherit by default is decades of accidental complexity,
historical layering, and interfaces that survived mainly because they were
already there.

RodNIX is a practical x86_64 operating system project focused on building a
small, debuggable, Unix-like system with explicit subsystem boundaries. The
goal is not to imitate an existing kernel line by line. The goal is to keep
the parts of Unix that aged well, and redesign the parts that would be done
more cleanly with today's constraints, hardware, and engineering standards in
mind.

If you care about kernel work, low-level debugging, and architecture that
stays readable as the project grows, this repository should feel familiar.

## Why Contribute

- Real kernel work, not toy stubs: scheduler, VM, syscalls, VFS, device model
- Fast feedback loop in QEMU
- Clear subsystem ownership (`posix`, `vm`, `fs`, `fabric`, `userland`)
- Contributions are expected to be small, reviewable, and testable

## Current Status

Implemented and working today:

- x86_64 boot path (Multiboot2, higher-half mapping)
- PMM + VM baseline (`mmap/munmap/brk`, COW groundwork)
- Interrupts, timers, preemptive scheduler
- VFS + RAMFS/initrd
- EXT2 read-only mount path
- Fabric bus/device/driver/service model + event stream
- IDE disk discovery and block service (`disk0`)
- Userland init + shell with pipes and redirects
- Growing POSIX ABI surface (`fork/exec/wait`, signals, poll/select, futex, etc.)

## Quick Start

### 1. Build and run

```bash
make clean
make
make iso
make run
```

Use verbose boot when needed:

```bash
make run -v
```

### 2. Useful shell commands inside RodNIX

- `hostinfo` / `sysinfo`
- `hwlist`, `fabricls`, `fabricevents`
- `scstat -a`
- `diskinfo`
- `fsapitest`, `syscalltest`, `sigtest`

## Where Help Is Needed Right Now

High-value contribution areas:

1. POSIX ABI hardening
   - pointer validation
   - syscall behavior parity
   - edge-case tests
2. File systems and storage
   - ext2 write path
   - block I/O reliability
3. Fabric evolution
   - richer service lifecycle
   - better service/subsystem separation
4. Userland quality
   - libc-lite growth
   - shell polish and utilities
5. Testing and CI
   - stronger contract and regression coverage

## First Contribution in 1-2 Hours

1. Pick one focused bug or missing check in one subsystem.
2. Reproduce with a command or test.
3. Implement the smallest correct fix.
4. Run baseline checks:
   - `make`
   - relevant smoke/contract script from `scripts/ci/`
5. Open PR with:
   - problem
   - what changed
   - validation evidence

## Repository Map

- `boot/` - boot code
- `kernel/` - kernel core and subsystems
- `drivers/` - hardware/Fabric drivers
- `userland/` - userspace binaries, libc-lite headers, shell
- `scripts/` - build and CI helpers
- `docs/` - documentation

## Development Rules

Contribution and review rules are in:

- `CONTRIBUTING.md`

Architecture and planning:

- `ARCHITECTURE.md`
- `ROADMAP.md`
- `docs/README.md`
- `docs/ru/README.md`
- `docs/en/README.md`

## License

See `LICENSE` and `ENTERPRISE_LICENSE.md`.
