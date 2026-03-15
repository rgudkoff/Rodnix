#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

LOG_FILE="${LOG_FILE:-boot.log}"
TIMEOUT_SEC="${TIMEOUT_SEC:-20}"
QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
QEMU_DISPLAY="${QEMU_DISPLAY:--display none}"
QEMU_NET_FLAGS="${QEMU_NET_FLAGS:--netdev user,id=net0 -device e1000,netdev=net0}"
ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-build/${ARCH}}"
ISO_PATH="${ISO_PATH:-${BUILD_DIR}/rodnix.iso}"
DISK_IMG="${DISK_IMG:-${BUILD_DIR}/rodnix-disk.img}"
DISK_MB="${DISK_MB:-128}"
DISK_FS_STAMP="${DISK_FS_STAMP:-${BUILD_DIR}/rodnix-disk.ext2.stamp}"
FLAG_FILE="userland/rootfs/etc/smoke.ifconfig.auto"

cleanup() {
  rm -f "$FLAG_FILE"
}
trap cleanup EXIT

dump_diag() {
  if [ ! -f "$LOG_FILE" ]; then
    echo "[smoke-ifconfig] no log file: $LOG_FILE"
    return
  fi
  echo "[smoke-ifconfig] recent markers:"
  grep "^\[SMK\]" "$LOG_FILE" | tail -n 20 || true
  echo "[smoke-ifconfig] last boot log lines:"
  tail -n 80 "$LOG_FILE" || true
}

touch "$FLAG_FILE"
rm -f "$LOG_FILE"

make iso ARCH="$ARCH"
mkdir -p "$(dirname "$DISK_IMG")"
if [ ! -f "$DISK_IMG" ]; then
  dd if=/dev/zero of="$DISK_IMG" bs=1m count="$DISK_MB" status=none
fi
if [ ! -f "$DISK_FS_STAMP" ]; then
  python3 scripts/mkext2_demo.py --output "$DISK_IMG" --size-mb "$DISK_MB"
  touch "$DISK_FS_STAMP"
fi

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  echo "[smoke-ifconfig] qemu not found: $QEMU_BIN"
  exit 1
fi

set +e
"$QEMU_BIN" -m 1G -boot d -cdrom "$ISO_PATH" ${QEMU_DISPLAY} -serial file:"$LOG_FILE" -no-reboot -no-shutdown \
  -drive file="$DISK_IMG",if=ide,format=raw,index=0,media=disk ${QEMU_NET_FLAGS} &
QEMU_PID=$!
set -e

deadline=$((SECONDS + TIMEOUT_SEC))
pass=0
prompt=0
while [ $SECONDS -lt $deadline ]; do
  if [ -f "$LOG_FILE" ]; then
    if grep -q "^\[SMK\] IFCONFIG PASS" "$LOG_FILE"; then
      pass=1
    fi
    if grep -q "sh> " "$LOG_FILE" || grep -q " # " "$LOG_FILE"; then
      prompt=1
    fi
    if grep -q "^\[SMK\] IFCONFIG FAIL" "$LOG_FILE"; then
      echo "[smoke-ifconfig] scenario reported FAIL"
      dump_diag
      kill "$QEMU_PID" >/dev/null 2>&1 || true
      exit 1
    fi
    if [ $pass -eq 1 ] && [ $prompt -eq 1 ]; then
      break
    fi
  fi
  sleep 1
done

if [ $pass -eq 1 ] && [ $prompt -eq 1 ]; then
  echo "[smoke-ifconfig] PASS: ifconfig completed and shell prompt reached"
  kill "$QEMU_PID" >/dev/null 2>&1 || true
  exit 0
fi

echo "[smoke-ifconfig] timeout waiting for pass markers"
dump_diag
kill "$QEMU_PID" >/dev/null 2>&1 || true
exit 1
