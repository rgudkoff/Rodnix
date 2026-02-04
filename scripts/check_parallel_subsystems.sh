#!/usr/bin/env bash
set -euo pipefail

# Fail if legacy parallel subsystems reappear.
# Keep this list small and factual.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

fail=0

check_path_absent() {
  local path="$1"
  local message="$2"
  if [ -e "${ROOT_DIR}/${path}" ]; then
    echo "[FAIL] ${message}: ${path}"
    fail=1
  fi
}

check_pattern_absent() {
  local pattern="$1"
  local message="$2"
  if grep -RIn "${pattern}" "${ROOT_DIR}" >/dev/null 2>&1; then
    echo "[FAIL] ${message}: pattern '${pattern}'"
    fail=1
  fi
}

# Removed legacy device manager (Fabric is the canonical path now).
check_path_absent "kernel/common/device.c" "Legacy device manager should not be reintroduced"
check_path_absent "kernel/common/device.h" "Legacy device manager should not be reintroduced"
check_pattern_absent "device_manager_init" "Legacy device manager reference should not reappear"

# Removed arch-specific keyboard driver (Fabric HID + InputCore is canonical).
check_path_absent "kernel/arch/x86_64/keyboard.c" "Arch-specific PS/2 keyboard driver should not be reintroduced"
check_path_absent "kernel/arch/x86_64/keyboard.h" "Arch-specific PS/2 keyboard driver should not be reintroduced"

# Removed interrupt registration stub.
check_path_absent "kernel/arch/x86_64/interrupts_stub.c" "Interrupt stub should not be reintroduced"

if [ "$fail" -ne 0 ]; then
  echo "[FAIL] Parallel/legacy subsystem guard failed."
  exit 1
fi

echo "[OK] Parallel/legacy subsystem guard passed."
