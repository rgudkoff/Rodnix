#!/usr/bin/env bash
# Exercise the UDP and TCP data paths end to end under QEMU.
#
# Covers the socket layer (net/socket.c) together with the protocol engine
# (net/proto_*.c): udptest drives sendto -> udp_proto_send -> netisr ->
# udp_deliver -> udp_proto_parse, tcptest drives listen/connect/accept and
# the TCP send/recv path.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

LOG_FILE="${LOG_FILE:-boot.log}"
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"
QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
QEMU_DISPLAY="${QEMU_DISPLAY:--display none}"
QEMU_NET_FLAGS="${QEMU_NET_FLAGS:--netdev user,id=net0 -device e1000,netdev=net0}"
ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-build/${ARCH}}"
ISO_PATH="${ISO_PATH:-${BUILD_DIR}/rodnix.iso}"
DISK_IMG="${DISK_IMG:-${BUILD_DIR}/rodnix-disk.img}"
DISK_MB="${DISK_MB:-128}"
DISK_FS_STAMP="${DISK_FS_STAMP:-${BUILD_DIR}/rodnix-disk.ext2.stamp}"
FLAG_FILE="userland/rootfs/etc/smoke.net.auto"

cleanup() {
  rm -f "$FLAG_FILE"
}
trap cleanup EXIT

dump_diag() {
  if [ ! -f "$LOG_FILE" ]; then
    echo "[smoke-net] no log file: $LOG_FILE"
    return
  fi
  echo "[smoke-net] markers:"
  grep "^\[SMK\]" "$LOG_FILE" | tail -n 20 || true
  echo "[smoke-net] last boot log lines:"
  tail -n 60 "$LOG_FILE" || true
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
  echo "[smoke-net] qemu not found: $QEMU_BIN"
  exit 1
fi

set +e
"$QEMU_BIN" -m 1G -boot d -cdrom "$ISO_PATH" ${QEMU_DISPLAY} -serial file:"$LOG_FILE" -no-reboot -no-shutdown \
  -drive file="$DISK_IMG",if=ide,format=raw,index=0,media=disk ${QEMU_NET_FLAGS} &
QEMU_PID=$!
set -e

deadline=$((SECONDS + TIMEOUT_SEC))
result=""
while [ $SECONDS -lt $deadline ]; do
  if [ -f "$LOG_FILE" ]; then
    if grep -q "^\[SMK\] NET PASS" "$LOG_FILE"; then
      result="pass"
      break
    fi
    if grep -q "^\[SMK\] NET FAIL" "$LOG_FILE"; then
      result="fail"
      break
    fi
  fi
  sleep 1
done

kill "$QEMU_PID" >/dev/null 2>&1 || true

if [ "$result" = "pass" ]; then
  echo "[smoke-net] PASS: udptest, tcptest and ping all completed"
  grep "^\[SMK\] \(UDPTEST\|TCPTEST\|PING\|NET\)" "$LOG_FILE" || true
  exit 0
fi

if [ "$result" = "fail" ]; then
  echo "[smoke-net] FAIL: a network test reported failure"
else
  echo "[smoke-net] timeout waiting for [SMK] NET marker"
fi
dump_diag
exit 1
