#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

LOG_FILE="${LOG_FILE:-boot.log}"
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"
QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
QEMU_DISPLAY="${QEMU_DISPLAY:--display none}"
ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-build/${ARCH}}"
ISO_PATH="${ISO_PATH:-${BUILD_DIR}/rodnix.iso}"
DISK_IMG="${DISK_IMG:-${BUILD_DIR}/rodnix-disk.img}"
FLAG_FILE="userland/rootfs/etc/tcc.auto"

cleanup() {
  rm -f "$FLAG_FILE"
}
trap cleanup EXIT

dump_diag() {
  if [ ! -f "$LOG_FILE" ]; then
    echo "[smoke-tcc] no log file: $LOG_FILE"
    return
  fi
  echo "[smoke-tcc] recent markers:"
  grep "^\[SMK\] TCC" "$LOG_FILE" | tail -n 20 || true
  echo "[smoke-tcc] last boot log lines:"
  tail -n 100 "$LOG_FILE" || true
}

printf 'auto\n' > "$FLAG_FILE"
rm -f "$LOG_FILE"

make -C userland -B ARCH="$ARCH" TOOLCHAIN=gcc
make tcc-disk ARCH="$ARCH"
make -B initrd iso ARCH="$ARCH"

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  echo "[smoke-tcc] qemu not found: $QEMU_BIN"
  exit 1
fi

set +e
"$QEMU_BIN" -m 1G -boot d -cdrom "$ISO_PATH" ${QEMU_DISPLAY} -serial file:"$LOG_FILE" -no-reboot -no-shutdown \
  -drive file="$DISK_IMG",if=ide,format=raw,index=0,media=disk &
QEMU_PID=$!
set -e

deadline=$((SECONDS + TIMEOUT_SEC))
pass=0
prompt=0
while [ $SECONDS -lt $deadline ]; do
  if [ -f "$LOG_FILE" ]; then
    if grep -q "^\[SMK\] TCC PASS" "$LOG_FILE"; then
      pass=1
    fi
    if grep -q "^\[SMK\] TCC FAIL" "$LOG_FILE" || grep -q "^\[SMK\] TCC TIMEOUT" "$LOG_FILE"; then
      echo "[smoke-tcc] scenario reported FAIL"
      dump_diag
      kill "$QEMU_PID" >/dev/null 2>&1 || true
      exit 1
    fi
    if grep -q "sh> " "$LOG_FILE" || grep -q " # " "$LOG_FILE" || grep -q " \$ " "$LOG_FILE" \
      || grep -q "RodNIX login" "$LOG_FILE" \
      || grep -q "^\[init\] service supervisor ready" "$LOG_FILE"; then
      prompt=1
    fi
    if [ $pass -eq 1 ] && [ $prompt -eq 1 ]; then
      break
    fi
  fi
  sleep 1
done

if [ $pass -eq 1 ] && [ $prompt -eq 1 ]; then
  echo "[smoke-tcc] PASS: tcc compiled smoke object and system reached ready state"
  kill "$QEMU_PID" >/dev/null 2>&1 || true
  exit 0
fi

echo "[smoke-tcc] timeout waiting for pass markers"
dump_diag
kill "$QEMU_PID" >/dev/null 2>&1 || true
exit 1
