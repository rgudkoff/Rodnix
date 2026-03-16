#!/usr/bin/env bash
# ============================================================
# build_tcc.sh — Cross-compile TCC for RodNIX (x86_64-elf)
#
# Usage:
#   ./scripts/build_tcc.sh [ARCH=x86_64]
#
# Output:
#   build/<ARCH>/tcc-staging/   — ready to inject into ext2 image
#     usr/bin/tcc
#     usr/lib/libc.a
#     usr/lib/crt0.o
#     usr/include/  (full RodNIX userland headers)
# ============================================================
set -euo pipefail

ARCH="${ARCH:-x86_64}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TCC_SRC="$ROOT_DIR/third_party/tcc"
BUILD_DIR="$ROOT_DIR/build/$ARCH"
USERLAND_BUILD="$ROOT_DIR/userland/build/$ARCH"
USERLAND_INC="$ROOT_DIR/userland/include"
STAGING="$BUILD_DIR/tcc-staging"
TCC_OUT="$BUILD_DIR/tcc"

case "$ARCH" in
  x86_64)
    TARGET_TRIPLE="x86_64-elf"
    TCC_TARGET_DEFINE="TCC_TARGET_X86_64"
    ;;
  arm64)
    TARGET_TRIPLE="aarch64-elf"
    TCC_TARGET_DEFINE="TCC_TARGET_ARM64"
    ;;
  riscv64)
    TARGET_TRIPLE="riscv64-elf"
    TCC_TARGET_DEFINE="TCC_TARGET_RISCV64"
    ;;
  *)
    echo "ERROR: unsupported ARCH '$ARCH'" >&2
    exit 1
    ;;
esac

CC="${TARGET_TRIPLE}-gcc"
AR="${TARGET_TRIPLE}-ar"

echo "[tcc] ARCH=$ARCH TARGET=$TARGET_TRIPLE CC=$CC"

# ── 1. Check cross-compiler ──────────────────────────────────
if ! command -v "$CC" &>/dev/null; then
    echo "ERROR: $CC not found. Install cross-toolchain first:" >&2
    echo "  macOS:  brew install x86_64-elf-gcc" >&2
    echo "  Ubuntu: apt-get install gcc-x86_64-linux-gnu  (then set CC=x86_64-linux-gnu-gcc)" >&2
    exit 1
fi

# ── 2. Fetch TCC source ──────────────────────────────────────
TCC_REPO="https://repo.or.cz/tinycc.git"
TCC_REF="release_0_9_28"
if [ ! -d "$TCC_SRC/.git" ]; then
    echo "[tcc] Cloning TCC $TCC_REF ..."
    git clone --depth=1 --branch "$TCC_REF" "$TCC_REPO" "$TCC_SRC"
else
    echo "[tcc] TCC source already at $TCC_SRC"
fi

# ── 3. Build userland (libc objects + crt0) ──────────────────
echo "[tcc] Building RodNIX userland ..."
make -C "$ROOT_DIR/userland" ARCH="$ARCH" TOOLCHAIN=gcc -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" \
    >/dev/null

# ── 4. Archive libc.a ────────────────────────────────────────
echo "[tcc] Archiving libc.a ..."
LIBC_OBJS=$(find "$USERLAND_BUILD/libc" -name "*.o" 2>/dev/null | sort | tr '\n' ' ')
if [ -z "$LIBC_OBJS" ]; then
    echo "ERROR: No libc .o files found in $USERLAND_BUILD/libc" >&2
    exit 1
fi
"$AR" rcs "$USERLAND_BUILD/libc.a" $LIBC_OBJS
CRT0="$USERLAND_BUILD/crt0.o"
if [ ! -f "$CRT0" ]; then
    echo "ERROR: $CRT0 not found" >&2
    exit 1
fi

# ── 5. Compile TCC via ONE_SOURCE ────────────────────────────
# We bypass TCC's configure and compile tcc.c directly.
# Key defines:
#   TCC_TARGET_X86_64      — generate x86_64 ELF code
#   ONE_SOURCE             — compile everything from tcc.c
#   CONFIG_TCC_STATIC      — no dlopen/plugin support
#   CONFIG_TCC_BCHECK      — disable bounds-check library (requires glibc internals)
#   CONFIG_RUNELF / TCC_IS_NATIVE — disable in-process execution (-run mode)
#   CONFIG_TCC_SYSINCLUDEPATHS — default system include search path inside RodNIX
#   CONFIG_TCC_LIBPATHS    — default library search path inside RodNIX
#   CONFIG_TCC_CRTPREFIX   — where to look for crt0.o, crti.o inside RodNIX
#   CONFIG_TCCDIR          — TCC's own resource dir (libtcc1.a, tcc headers)
echo "[tcc] Compiling TCC for RodNIX ..."
mkdir -p "$BUILD_DIR"
	"$CC" \
	    -DONE_SOURCE=1 \
	    "-D${TCC_TARGET_DEFINE}=1" \
	    -DCONFIG_TCC_STATIC=1 \
    -DCONFIG_TCC_BCHECK=0 \
    -DTCC_IS_NATIVE=0 \
    "-DTCC_VERSION=\"0.9.28\"" \
    "-DCONFIG_TCC_SYSINCLUDEPATHS=\"/usr/include:/usr/lib/tcc\"" \
    "-DCONFIG_TCC_LIBPATHS=\"/usr/lib\"" \
    "-DCONFIG_TCC_CRTPREFIX=\"/usr/lib\"" \
    "-DCONFIG_TCCDIR=\"/usr/lib/tcc\"" \
    -O2 \
    -fno-stack-protector \
    -nostdinc -I"$USERLAND_INC" \
    -nostdlib -static \
    "$TCC_SRC/tcc.c" \
    -o "$TCC_OUT" \
    "$CRT0" "$USERLAND_BUILD/libc.a"

echo "[tcc] TCC binary: $TCC_OUT ($(du -h "$TCC_OUT" | cut -f1))"

# ── 6. Build libtcc1.a (TCC runtime helpers) ─────────────────
# libtcc1.c provides helpers for things like 64-bit multiply-high,
# va_list helpers, etc. Compile with cross-compiler.
echo "[tcc] Building libtcc1.a ..."
LIBTCC1_DIR="$BUILD_DIR/libtcc1"
mkdir -p "$LIBTCC1_DIR"
"$CC" \
    -c "$TCC_SRC/lib/libtcc1.c" \
    -O2 -fno-stack-protector \
    -nostdinc -I"$USERLAND_INC" -I"$TCC_SRC" \
    -o "$LIBTCC1_DIR/libtcc1.o" \
    2>/dev/null || echo "[tcc] Warning: libtcc1.c failed to compile — TCC will work without fp/va helpers"
if [ -f "$LIBTCC1_DIR/libtcc1.o" ]; then
    "$AR" rcs "$BUILD_DIR/libtcc1.a" "$LIBTCC1_DIR/libtcc1.o"
fi

# ── 7. Stage ─────────────────────────────────────────────────
echo "[tcc] Staging to $STAGING ..."
rm -rf "$STAGING"
mkdir -p "$STAGING/usr/bin"
mkdir -p "$STAGING/usr/lib/tcc"
mkdir -p "$STAGING/usr/include"

cp "$TCC_OUT"                   "$STAGING/usr/bin/tcc"
cp "$USERLAND_BUILD/libc.a"     "$STAGING/usr/lib/libc.a"
cp "$CRT0"                      "$STAGING/usr/lib/crt0.o"
if [ -f "$BUILD_DIR/libtcc1.a" ]; then
    cp "$BUILD_DIR/libtcc1.a"   "$STAGING/usr/lib/tcc/libtcc1.a"
fi
cp -r "$USERLAND_INC/." "$STAGING/usr/include/"

echo ""
echo "[tcc] ✓ Done. Staging directory:"
find "$STAGING" -type f | sort | sed "s|$STAGING/||"
echo ""
echo "Next: make ARCH=$ARCH qemu-disk   (will inject staging into disk image)"
