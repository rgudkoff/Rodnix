#!/usr/bin/env bash
# Verify that the MADT processor inventory matches the guest CPU count.
#
# Boots the current ISO at several -smp values and checks that cpu_topology
# reports exactly that many processors. Requires the ISO to carry a verbose
# boot cmdline so the per-CPU lines are emitted.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

TIMEOUT_SEC="${TIMEOUT_SEC:-30}"
QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
QEMU_DISPLAY="${QEMU_DISPLAY:--display none}"
QEMU_CPU="${QEMU_CPU:-qemu64,+apic,+x2apic}"
ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-build/${ARCH}}"
ISO_PATH="${ISO_PATH:-${BUILD_DIR}/rodnix.iso}"
DISK_IMG="${DISK_IMG:-${BUILD_DIR}/rodnix-disk.img}"
DISK_MB="${DISK_MB:-128}"
DISK_FS_STAMP="${DISK_FS_STAMP:-${BUILD_DIR}/rodnix-disk.ext2.stamp}"
SMP_LIST="${SMP_LIST:-1 2 4 8}"
VERBOSE_CMDLINE="bootverbose verbose_sysinit=1 startup_debug=verbose bootlog=verbose"

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  echo "[smp] qemu not found: $QEMU_BIN"
  exit 1
fi

make -B initrd iso ARCH="$ARCH" KERNEL_CMDLINE="$VERBOSE_CMDLINE"

mkdir -p "$(dirname "$DISK_IMG")"
if [ ! -f "$DISK_IMG" ]; then
  dd if=/dev/zero of="$DISK_IMG" bs=1m count="$DISK_MB" status=none
fi
if [ ! -f "$DISK_FS_STAMP" ]; then
  python3 scripts/mkext2_demo.py --output "$DISK_IMG" --size-mb "$DISK_MB"
  touch "$DISK_FS_STAMP"
fi

rc=0
for n in $SMP_LIST; do
  log="$(mktemp -t rodnix-smp-XXXXXX)"
  rm -f "$log"

  "$QEMU_BIN" -m 1G -boot d -cdrom "$ISO_PATH" ${QEMU_DISPLAY} \
    -serial file:"$log" -no-reboot -no-shutdown \
    -drive file="$DISK_IMG",if=ide,format=raw,index=0,media=disk \
    -machine pc -smp "$n" -cpu "$QEMU_CPU" &
  qemu_pid=$!

  deadline=$((SECONDS + TIMEOUT_SEC))
  while [ $SECONDS -lt $deadline ]; do
    if [ -f "$log" ] && grep -q "^\[CPU-TOPO\] .* processor(s)" "$log"; then
      break
    fi
    sleep 1
  done
  kill "$qemu_pid" >/dev/null 2>&1 || true
  wait "$qemu_pid" 2>/dev/null || true

  summary="$(grep -m1 "^\[CPU-TOPO\] .* processor(s)" "$log" 2>/dev/null || true)"
  if [ -z "$summary" ]; then
    echo "[smp] -smp $n: FAIL (no topology report within ${TIMEOUT_SEC}s)"
    tail -n 20 "$log" 2>/dev/null || true
    rc=1
    rm -f "$log"
    continue
  fi

  reported="$(printf '%s\n' "$summary" | sed -E 's/^\[CPU-TOPO\] ([0-9]+) processor\(s\).*/\1/')"
  listed="$(grep -c "^\[CPU-TOPO\]   cpu" "$log" || true)"

  if [ "$reported" = "$n" ] && [ "$listed" = "$n" ]; then
    echo "[smp] -smp $n: PASS ($summary)"
  else
    echo "[smp] -smp $n: FAIL (reported=$reported listed=$listed expected=$n)"
    grep "^\[CPU-TOPO\]" "$log" || true
    rc=1
  fi
  rm -f "$log"
done

if [ $rc -eq 0 ]; then
  echo "[smp] processor inventory matches guest CPU count for: $SMP_LIST"
fi
exit $rc
