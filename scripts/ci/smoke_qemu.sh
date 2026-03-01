#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

LOG_FILE="${LOG_FILE:-boot.log}"
TIMEOUT_SEC="${TIMEOUT_SEC:-10}"
QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"

rm -f "$LOG_FILE"

make iso

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  echo "[smoke] qemu not found: $QEMU_BIN"
  exit 1
fi

set +e
"$QEMU_BIN" -m 512M -boot d -cdrom rodnix.iso -serial file:"$LOG_FILE" -no-reboot -no-shutdown &
QEMU_PID=$!
set -e

deadline=$((SECONDS + TIMEOUT_SEC))
found=0
status_msg=""
while [ $SECONDS -lt $deadline ]; do
  if [ -f "$LOG_FILE" ]; then
    if grep -q "rodnix>" "$LOG_FILE"; then
      found=1
      status_msg="[smoke] kernel shell prompt detected"
      break
    fi
    if grep -q "\[USER\] init: POSIX smoke test done" "$LOG_FILE"; then
      found=1
      status_msg="[smoke] userspace init completed"
      break
    fi
  fi
  sleep 1
done

if [ $found -eq 1 ]; then
  echo "${status_msg:-[smoke] boot marker detected}"
  kill "$QEMU_PID" >/dev/null 2>&1 || true
  exit 0
fi

echo "[smoke] timeout waiting for boot marker (shell prompt or userspace init)"
kill "$QEMU_PID" >/dev/null 2>&1 || true
exit 1
