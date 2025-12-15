# kernel - RodNIX Core Kernel

The `kernel` directory contains the core operating system kernel.

## Structure

```
kernel/
├── core/           # Architecture-independent abstractions
├── common/         # Common kernel components
└── arch/           # Architecture-dependent implementations
    ├── x86_64/     # x86_64 (CISC)
    ├── arm64/      # ARM64 (RISC)
    └── riscv64/    # RISC-V64 (RISC)
```

## Principles

1. **Architectural abstraction**: All architecture-dependent details are isolated in `arch/`
2. **Unified interface**: `core/` provides unified interfaces for all architectures
3. **Modularity**: Each component is clearly separated and can be replaced

## Components

### core/
Architecture-independent interfaces and abstractions:
- `arch_types.h` - basic data types
- `config.h` - kernel configuration
- Interfaces for interrupts, memory, CPU, etc.

### common/
Common kernel components working on top of abstractions:
- Scheduler
- Memory management
- IPC
- Device drivers

### arch/
Architecture-dependent implementations:
- Each architecture implements interfaces from `core/`
- Contains architecture-specific optimizations
- Isolates differences between CISC and RISC
