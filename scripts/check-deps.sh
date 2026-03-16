#!/bin/bash
# Check build dependencies for RodNIX

echo "Checking RodNIX build dependencies..."
echo ""

MISSING=0

# Check for cross-compiler (64-bit)
if command -v x86_64-elf-gcc >/dev/null 2>&1; then
    echo "[OK] x86_64-elf-gcc: $(which x86_64-elf-gcc)"
elif command -v i686-elf-gcc >/dev/null 2>&1; then
    echo "[WARN] i686-elf-gcc found, but 64-bit compiler needed"
    echo "  Install: brew install x86_64-elf-gcc (macOS)"
    MISSING=1
else
    echo "[MISSING] x86_64-elf-gcc"
    echo "  Install: brew install x86_64-elf-gcc (macOS) or use package manager (Linux)"
    MISSING=1
fi

# Check for linker (64-bit)
if command -v x86_64-elf-ld >/dev/null 2>&1; then
    echo "[OK] x86_64-elf-ld: $(which x86_64-elf-ld)"
else
    echo "[MISSING] x86_64-elf-ld"
    echo "  Install: brew install x86_64-elf-binutils (macOS)"
    MISSING=1
fi

# Check for NASM
if command -v nasm >/dev/null 2>&1; then
    echo "[OK] nasm: $(which nasm)"
else
    echo "[MISSING] nasm"
    echo "  Install: brew install nasm (macOS) or sudo apt-get install nasm (Linux)"
    MISSING=1
fi

# Check for QEMU (64-bit)
if command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "[OK] qemu-system-x86_64: $(which qemu-system-x86_64)"
elif command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "[WARN] qemu-system-i386 found, but 64-bit QEMU needed"
    echo "  Install: brew install qemu (macOS) or sudo apt-get install qemu-system-x86 (Linux)"
    MISSING=1
else
    echo "[MISSING] qemu-system-x86_64"
    echo "  Install: brew install qemu (macOS) or sudo apt-get install qemu-system-x86 (Linux)"
    MISSING=1
fi

# Check for GRUB or Limine (for ISO creation)
if command -v grub-mkrescue >/dev/null 2>&1; then
    echo "[OK] grub-mkrescue: $(which grub-mkrescue)"
elif command -v limine >/dev/null 2>&1 && command -v xorriso >/dev/null 2>&1; then
    echo "[OK] Limine: $(which limine) (will be used for ISO creation)"
    echo "[OK] xorriso: $(which xorriso)"
else
    echo "[WARN] Neither grub-mkrescue nor Limine found"
    echo "  Install options:"
    echo "    macOS: sudo port install grub2 xorriso (MacPorts)"
    echo "    macOS: brew install limine xorriso (Homebrew - recommended)"
    echo "    Linux: sudo apt-get install grub-pc-bin xorriso"
fi

echo ""
if [ $MISSING -eq 0 ]; then
    echo "All required dependencies are installed!"
    echo "You can build the kernel with: make"
else
    echo "Some required dependencies are missing."
    echo "Please install them before building."
    exit 1
fi

