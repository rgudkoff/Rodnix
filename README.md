# RodNIX

RodNIX is what Unix might look like if it were designed from scratch today.

RodNIX keeps the parts of the Unix model that still matter: small tools, clear
interfaces, process isolation, and a system that can be understood end to end.
What it does not inherit by default is decades of accidental complexity,
historical layering, and interfaces that survived mainly because they were
already there.

RodNIX is a practical x86_64 operating system project focused on building a
small, debuggable Unix-like system with explicit subsystem boundaries. The goal
is not to imitate an existing kernel line by line. The goal is to keep the
parts of Unix that aged well and redesign the parts that would be implemented
more cleanly with today's constraints, hardware, and engineering standards.

If you enjoy kernel development, low-level debugging, and architectures that
remain readable as they evolve, RodNIX should feel familiar.

## Architecture

RodNIX introduces a Fabric-based system model.

Instead of scattering discovery logic and driver lifecycle across unrelated
subsystems, devices, drivers, and services are represented as nodes in a
unified system fabric.

The Fabric layer provides:

- device discovery
- driver binding
- subsystem registration
- service lifecycle control
- system-wide event stream

This approach makes hardware discovery and subsystem interaction observable,
debuggable, and structurally consistent.

## System Overview

```text
            +----------------------+
            |       Userland       |
            |  shell / utilities   |
            +----------+-----------+
                       |
                    POSIX ABI
                       |
       +---------------+---------------+
       |            Kernel Core        |
       | scheduler | vm | vfs | ipc    |
       +---------------+---------------+
                       |
                     Fabric
          device / driver / service bus
                       |
       +---------------+---------------+
       | PCI | IDE | platform | virt   |
       +---------------+---------------+
```

## Project Goals

RodNIX prioritizes:

- readable kernel architecture
- explicit subsystem boundaries
- fast debugging in virtual environments
- incremental POSIX compatibility
- experimentation with modern kernel design ideas

RodNIX does not aim to:

- replace Linux
- replicate historical Unix behavior blindly
- support every legacy interface

This project exists primarily as a clean kernel architecture experiment.

## Current Status

Implemented and working today:

- x86_64 boot path (Multiboot2, higher-half mapping)
- PMM + VM baseline (`mmap`, `munmap`, `brk`)
- copy-on-write groundwork
- interrupts and LAPIC timer
- preemptive scheduler
- VFS layer with RAMFS / initrd
- EXT2 read-only mount path
- Fabric device / driver / service model
- system event stream
- IDE disk discovery and block service (`disk0`)
- userland init and shell
- pipes and redirects
- growing POSIX syscall surface
- `fork`
- `exec`
- `wait`
- signals
- `poll` / `select`
- futex

Primary development platform: x86_64 + QEMU.

## Quick Start

Build and run the system:

```bash
make clean
make
make iso
make run
```

Verbose boot output:

```bash
make run -v
```

## Useful Shell Commands

Inside RodNIX:

- `hostinfo` / `sysinfo`
- `hwlist`
- `fabricls`
- `fabricevents`
- `scstat -a`
- `diskinfo`
- `fsapitest`
- `syscalltest`
- `sigtest`

These utilities help inspect kernel subsystems and test interfaces.

## Where Help Is Needed

High-impact contribution areas:

- POSIX ABI hardening: pointer validation, syscall behavior parity, edge case
  testing
- File systems and storage: EXT2 write path, block I/O reliability, caching
  and buffering improvements
- Fabric evolution: richer service lifecycle, improved service/subsystem
  boundaries, event stream extensions
- Userland: libc-lite expansion, shell improvements, additional utilities
- Testing: regression tests, subsystem contract tests, CI pipeline improvements

## First Contribution (1-2 Hours)

A typical first contribution looks like this:

1. Pick one small bug or missing validation check.
2. Reproduce the issue using a command or test.
3. Implement the smallest correct fix.
4. Run baseline checks.

```bash
make
scripts/ci/smoke_qemu.sh
```

5. Open a Pull Request describing:

- the problem
- what changed
- how it was validated

Small, focused patches are strongly preferred.

## Repository Structure

- `boot/` - boot code
- `kernel/` - kernel core and subsystems
- `drivers/` - hardware and Fabric drivers
- `userland/` - userspace binaries and shell
- `scripts/` - build and CI helpers
- `docs/` - documentation

## Development

Contribution guidelines:

- `CONTRIBUTING.md`

Architecture and planning:

- `ARCHITECTURE.md`
- `ROADMAP.md`
- `docs/README.md`
- `docs/en/README.md`
- `docs/ru/README.md`

## License

See:

- `LICENSE`
- `ENTERPRISE_LICENSE.md`
