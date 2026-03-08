# Installation Guide

## Quick Start

### macOS

```bash
# Install dependencies
brew install x86_64-elf-gcc x86_64-elf-binutils nasm qemu docker

# For GRUB (optional - Docker will be used as fallback):
# Option 1: MacPorts
#   sudo port install grub2
# Option 2: Use Docker (automatic fallback in Makefile)

# Build and run
make clean && make && make iso && make run
```

**Note**: Homebrew does not provide `grub-mkrescue`. The Makefile will automatically use Docker if grub-mkrescue is not found.

### Ubuntu/Debian

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install build-essential nasm qemu-system-x86 grub-pc-bin grub-common

# For cross-compiler (if not available in repos):
# Download from any trusted cross-toolchain source (or similar)

# Build and run
make clean && make && make iso && make run
```

## Manual Installation Steps

### 1. Cross-Compiler Toolchain

**macOS:**
```bash
brew tap nativeos/i386-elf-toolchain
brew install x86_64-elf-gcc x86_64-elf-binutils
```

**Ubuntu/Debian:**
- Use your distribution's package manager
- Or build from source: https://wiki.osdev.org/GCC_Cross-Compiler

### 2. NASM

**macOS:**
```bash
brew install nasm
```

**Ubuntu/Debian:**
```bash
sudo apt-get install nasm
```

### 3. QEMU

**macOS:**
```bash
brew install qemu
```

**Ubuntu/Debian:**
```bash
sudo apt-get install qemu-system-x86
```

### 4. GRUB (for ISO creation)

**macOS:**
```bash
brew install grub
```

**Ubuntu/Debian:**
```bash
sudo apt-get install grub-pc-bin grub-common
```

## Verification

Check that all tools are installed:

```bash
which x86_64-elf-gcc
which nasm
which qemu-system-x86_64
which grub-mkrescue
```

All commands should return paths to the executables.

## Building

```bash
# Clean previous build
make clean

# Build kernel
make

# Create ISO (requires grub-mkrescue)
make iso

# Run in QEMU
make run
```

## Troubleshooting

See [docs/BUILD.md](docs/BUILD.md) for detailed troubleshooting information.
