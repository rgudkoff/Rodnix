#!/bin/bash
set -euo pipefail

IMAGE_PATH="${1:-}"
DEVICE_PATH="${2:-}"

usage() {
    cat <<'EOF'
Usage:
  scripts/flash_usb.sh <image-path> <device-path>

Examples:
  scripts/flash_usb.sh build/x86_64/rodnix-usb.img /dev/disk4
  scripts/flash_usb.sh build/x86_64/rodnix-usb.img /dev/sdb

Notes:
  - This script writes a full disk image to the target device.
  - The target must be the whole disk, not a partition.
  - Double-check the device path before running it.
EOF
}

fail() {
    echo "[!] $*" >&2
    exit 1
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "Required tool '$1' not found"
}

if [[ -z "${IMAGE_PATH}" || -z "${DEVICE_PATH}" ]]; then
    usage
    exit 1
fi

[[ -f "${IMAGE_PATH}" ]] || fail "Image not found: ${IMAGE_PATH}"

OS_NAME="$(uname -s)"

flash_darwin() {
    local image="$1"
    local dev="$2"
    local raw_dev="$dev"

    need_cmd diskutil
    need_cmd dd
    need_cmd sync
    need_cmd sudo

    [[ "${dev}" == /dev/disk* || "${dev}" == /dev/rdisk* ]] || \
        fail "On macOS use a whole-disk device like /dev/disk4 or /dev/rdisk4"

    if [[ "${dev}" == /dev/rdisk* ]]; then
        dev="/dev/disk${dev#/dev/rdisk}"
    fi
    raw_dev="/dev/rdisk${dev#/dev/disk}"

    diskutil info "${dev}" >/dev/null 2>&1 || fail "Device not found: ${dev}"

    if diskutil info "${dev}" | grep -qE '^\s*Whole:\s*No$'; then
        fail "Target must be a whole disk, not a partition: ${dev}"
    fi

    if diskutil info "${dev}" | grep -qE '^\s*Internal:\s*Yes$'; then
        fail "Refusing to overwrite an internal disk: ${dev}"
    fi

    echo "[*] Unmounting ${dev} ..."
    diskutil unmountDisk force "${dev}"

    echo "[*] Writing ${image} -> ${raw_dev} ..."
    sudo dd if="${image}" of="${raw_dev}" bs=4m status=progress
    sync

    echo "[*] Ejecting ${dev} ..."
    diskutil eject "${dev}" || true
}

flash_linux() {
    local image="$1"
    local dev="$2"

    need_cmd dd
    need_cmd sync
    need_cmd sudo
    need_cmd lsblk

    [[ "${dev}" == /dev/* ]] || fail "On Linux use a block device path like /dev/sdb"
    lsblk -dn "${dev}" >/dev/null 2>&1 || fail "Device not found: ${dev}"

    local dev_type
    dev_type="$(lsblk -dno TYPE "${dev}" 2>/dev/null || true)"
    [[ "${dev_type}" == "disk" ]] || fail "Target must be a whole disk, not a partition: ${dev}"

    local hotplug
    hotplug="$(lsblk -dno HOTPLUG "${dev}" 2>/dev/null || true)"
    if [[ -n "${hotplug}" && "${hotplug}" != "1" ]]; then
        fail "Refusing to write to a non-hotplug disk: ${dev}"
    fi

    while read -r part mountpoint; do
        [[ -n "${part}" ]] || continue
        if [[ -n "${mountpoint}" ]]; then
            echo "[*] Unmounting ${part} ..."
            sudo umount "${part}"
        fi
    done < <(lsblk -lnpo NAME,MOUNTPOINT "${dev}" | tail -n +2)

    echo "[*] Writing ${image} -> ${dev} ..."
    sudo dd if="${image}" of="${dev}" bs=4M status=progress conv=fsync
    sync
}

case "${OS_NAME}" in
    Darwin)
        flash_darwin "${IMAGE_PATH}" "${DEVICE_PATH}"
        ;;
    Linux)
        flash_linux "${IMAGE_PATH}" "${DEVICE_PATH}"
        ;;
    *)
        fail "Unsupported host OS: ${OS_NAME}"
        ;;
esac

echo "[+] USB flash complete"
