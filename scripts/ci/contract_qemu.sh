#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

LOG_FILE="${LOG_FILE:-boot.log}"
TIMEOUT_SEC="${TIMEOUT_SEC:-20}"
QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
FLAG_FILE="userland/rootfs/etc/contract.auto"

cleanup() {
  rm -f "$FLAG_FILE"
}
trap cleanup EXIT

dump_diag() {
  if [ ! -f "$LOG_FILE" ]; then
    echo "[contract] no log file: $LOG_FILE"
    return
  fi
  echo "[contract] recent [CT] markers:"
  grep "^\[CT\]" "$LOG_FILE" | tail -n 20 || true
  echo "[contract] last boot log lines:"
  tail -n 60 "$LOG_FILE" || true
}

touch "$FLAG_FILE"
rm -f "$LOG_FILE"

make iso

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  echo "[contract] qemu not found: $QEMU_BIN"
  exit 1
fi

set +e
"$QEMU_BIN" -m 512M -boot d -cdrom rodnix.iso -serial file:"$LOG_FILE" -no-reboot -no-shutdown &
QEMU_PID=$!
set -e

deadline=$((SECONDS + TIMEOUT_SEC))
found=0
while [ $SECONDS -lt $deadline ]; do
  if [ -f "$LOG_FILE" ]; then
    if grep -q "^\[CT\] ALL PASS" "$LOG_FILE"; then
      found=1
      break
    fi
    if grep -q "^\[CT\] ALL FAIL" "$LOG_FILE"; then
      echo "[contract] contract suite reported FAIL"
      dump_diag
      kill "$QEMU_PID" >/dev/null 2>&1 || true
      exit 1
    fi
  fi
  sleep 1
done

if [ $found -eq 1 ]; then
  echo "[contract] contract markers detected: ALL PASS"
  kill "$QEMU_PID" >/dev/null 2>&1 || true
  exit 0
fi

echo "[contract] timeout waiting for [CT] ALL PASS"
dump_diag
kill "$QEMU_PID" >/dev/null 2>&1 || true
exit 1
