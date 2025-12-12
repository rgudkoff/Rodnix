# RodNIX

RodNIX is an experimental operating system kernel project targeting **x86_64** architecture.  
The project focuses on clarity, security-oriented design, and gradual evolution toward a modern, modular OS architecture.

RodNIX is developed primarily as a learning and research platform, but with engineering discipline and long-term extensibility in mind.

---

## Project goals

- Build a **clean and understandable kernel** without legacy constraints
- Use **x86_64 as the primary target**, with a Multiboot2 32-bit entry stub
- Separate **architecture-independent core** from **arch-specific code**
- Gradually **move drivers out of the kernel** to reduce attack surface
- Favor **explicit design over hidden magic**
- Provide a solid foundation for future experimentation (SMP, userspace, microkernel concepts)

---

## Current state

RodNIX is in **early active development**.

Implemented so far:

- Multiboot2-compliant boot sequence
- 32-bit boot stub with transition toward 64-bit long mode
- GDT / IDT initialization
- ISR and IRQ handling
- PIC remapping
- PIT timer initialization
- VGA text-mode console
- Minimal interactive shell
- PS/2 keyboard input (scancode → ASCII translation, buffering, modifiers)
- Early memory and paging groundwork
- ISO image generation and QEMU support

Expect frequent changes and refactoring.

---

## Architecture overview

High-level structure:

RodNIX/
├── boot/ # Early boot code (Multiboot2, 32-bit entry)
├── kernel/
│ ├── common/ # Architecture-independent kernel code
│ ├── core/ # Core kernel subsystems
│ └── arch/
│ └── x86_64/ # x86_64-specific implementation
├── drivers/ # Early drivers (planned to move out of kernel)
├── include/ # Public kernel headers
├── docs/ # Design and migration documents
├── Makefile
└── link.ld


Detailed design notes:
- `ARCHITECTURE.md`
- `64BIT_MIGRATION.md`

---

## Build requirements

### Toolchain

- `x86_64-elf-gcc` (or i686 cross-compiler for early stages)
- `nasm`
- `ld` (from binutils)
- `qemu-system-x86_64`
- `xorriso`
- `mtools`
- `grub-mkrescue`

### macOS (Homebrew)

```bash
brew install x86_64-elf-gcc x86_64-elf-binutils nasm qemu xorriso mtools
Note: grub-mkrescue on macOS may require additional setup.
Building on Linux is currently the most reliable option.

### Ubuntu / Debian
```bash
sudo apt update
sudo apt install -y \
  build-essential \
  nasm \
  qemu-system-x86 \
  xorriso \
  mtools \
  grub-pc-bin


### Keyboard and shell

PS/2 keyboard input with internal buffering

Scancode-to-ASCII translation

Modifier state handling (Shift, Caps Lock)

Simple interactive shell

Line-based input with basic editing

The keyboard logic is currently low-level and architecture-specific by design.
