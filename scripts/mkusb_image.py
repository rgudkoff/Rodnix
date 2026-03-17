#!/usr/bin/env python3
"""
mkusb_image.py — Create a bootable USB disk image for RodNIX.

Disk layout:
  Sector 0          — MBR + partition table (Limine BIOS bootstrap installed here)
  Partition 1       — FAT32 (64 MiB)  — boot: kernel, initrd, Limine files
  Partition 2       — ext2  (rest)    — data: /etc, /home, /usr, …

Usage:
  python3 scripts/mkusb_image.py \\
      --kernel build/x86_64/rodnix.kernel \\
      --initrd  build/x86_64/initrd.img   \\
      --output  build/rodnix-usb.img      \\
      [--size-mb 512] [--ext2-inject-dir path/to/staging]

Flash to USB:
  dd if=build/rodnix-usb.img of=/dev/sdX bs=4M status=progress && sync

Requirements:
  - mtools  (mformat, mmd, mcopy)   — brew install mtools / apt install mtools
  - limine  (bios-install + files)  — brew install limine / from limine.unreliable.technology
"""

from __future__ import annotations

import argparse
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SECTOR = 512
MiB = 1024 * 1024

# Partition 1: FAT32 boot — 64 MiB
FAT_PART_SIZE_MiB = 64

# First partition starts at 1 MiB (2048 sectors) — standard alignment
P1_START_LBA = 2048


# ---------------------------------------------------------------------------
# MBR helpers
# ---------------------------------------------------------------------------

def _lba_to_chs(lba: int) -> tuple[int, int, int]:
    """Convert LBA to CHS tuple (head, sector_byte, cylinder_low).

    The sector byte packs the upper 2 bits of the cylinder into bits 7:6.
    Capped at the max CHS range understood by old BIOSes.
    """
    heads_per_cyl = 255
    spt = 63
    c = min(lba // (heads_per_cyl * spt), 1023)
    h = (lba // spt) % heads_per_cyl
    s = (lba % spt) + 1
    sector_byte = s | ((c >> 2) & 0xC0)
    cylinder_low = c & 0xFF
    return h, sector_byte, cylinder_low


def _mbr_part_entry(bootable: bool, ptype: int, lba_start: int, lba_count: int) -> bytes:
    sh, ss, sc = _lba_to_chs(lba_start)
    eh, es, ec = _lba_to_chs(lba_start + lba_count - 1)
    return struct.pack(
        "<BBBBBBBBII",
        0x80 if bootable else 0x00,
        sh, ss, sc,
        ptype,
        eh, es, ec,
        lba_start,
        lba_count,
    )


def write_mbr(img_path: str, p1_lba: int, p1_sectors: int,
              p2_lba: int, p2_sectors: int) -> None:
    """Write a 512-byte MBR with two partition entries, leaving room for
    Limine BIOS code (written later by `limine bios-install`)."""
    mbr = bytearray(512)
    # Tiny jmp + nop as placeholder boot code (Limine overwrites this).
    mbr[0] = 0xEB
    mbr[1] = 0x5A
    mbr[2] = 0x90
    # Random disk signature
    struct.pack_into("<I", mbr, 440, random.randint(0, 0xFFFF_FFFF))
    mbr[444:446] = b'\x00\x00'  # reserved, must be zero
    # Partition 1: FAT32 LBA (0x0C = FAT32 with LBA)
    mbr[446:462] = _mbr_part_entry(True, 0x0C, p1_lba, p1_sectors)
    # Partition 2: Linux native (0x83)
    mbr[462:478] = _mbr_part_entry(False, 0x83, p2_lba, p2_sectors)
    # Boot signature
    mbr[510] = 0x55
    mbr[511] = 0xAA
    with open(img_path, "r+b") as f:
        f.seek(0)
        f.write(mbr)


# ---------------------------------------------------------------------------
# Tool helpers
# ---------------------------------------------------------------------------

def _require(tool: str, hint: str) -> str:
    path = shutil.which(tool)
    if path is None:
        sys.exit(f"[!] Required tool '{tool}' not found.\n    {hint}")
    return path


def _run(*cmd: str, **kw) -> None:
    result = subprocess.run(list(cmd), **kw)
    if result.returncode != 0:
        sys.exit(f"[!] Command failed: {' '.join(cmd)}")


# ---------------------------------------------------------------------------
# FAT32 partition setup via mtools
# ---------------------------------------------------------------------------

def _mtools_img(img_path: str, p1_offset_bytes: int) -> str:
    """Return the mtools image specifier for partition 1."""
    return f"{img_path}@@{p1_offset_bytes}"


def setup_fat32(img_path: str, p1_offset: int, p1_size_bytes: int,
                kernel: str, initrd: str | None, limine_dir: str,
                cfg_path: str) -> None:
    mformat = _require("mformat", "brew install mtools  OR  apt install mtools")
    mmd     = _require("mmd",     "brew install mtools  OR  apt install mtools")
    mcopy   = _require("mcopy",   "brew install mtools  OR  apt install mtools")

    spec = _mtools_img(img_path, p1_offset)

    # Format as FAT32 with 4-sector clusters (2 KiB)
    print("[*] Formatting FAT32 boot partition …")
    _run(mformat, "-i", spec, "-F", "-v", "RODNIX BOOT", "-c", "4", "::")

    # Directory structure
    for d in ["::/boot", "::/EFI", "::/EFI/BOOT"]:
        _run(mmd, "-i", spec, d)

    # Kernel
    print(f"[*] Copying kernel → /boot/rodnix.kernel")
    _run(mcopy, "-i", spec, kernel, "::/boot/rodnix.kernel")

    # Initrd (optional — may not exist yet)
    if initrd and os.path.exists(initrd):
        print(f"[*] Copying initrd → /boot/initrd.img")
        _run(mcopy, "-i", spec, initrd, "::/boot/initrd.img")
    else:
        print("[~] Initrd not found, skipping (add later with 'make initrd')")

    # Limine config — look at boot/limine.cfg, patch CMDLINE if needed
    print("[*] Copying limine.cfg")
    _run(mcopy, "-i", spec, cfg_path, "::/limine.cfg")
    _run(mcopy, "-i", spec, cfg_path, "::/boot/limine.cfg")

    # Limine BIOS system file (needed at runtime on BIOS systems)
    bios_sys = os.path.join(limine_dir, "limine-bios.sys")
    if os.path.exists(bios_sys):
        _run(mcopy, "-i", spec, bios_sys, "::/limine-bios.sys")
    else:
        print(f"[~] {bios_sys} not found — BIOS boot may fail")

    # UEFI bootloader
    for efi_name in ["BOOTX64.EFI", "bootx64.efi"]:
        efi_src = os.path.join(limine_dir, efi_name)
        if os.path.exists(efi_src):
            _run(mcopy, "-i", spec, efi_src, "::/EFI/BOOT/BOOTX64.EFI")
            _run(mcopy, "-i", spec, cfg_path, "::/EFI/BOOT/limine.cfg")
            print(f"[*] Copied UEFI bootloader → /EFI/BOOT/BOOTX64.EFI")
            break
    else:
        print("[~] UEFI EFI binary not found, skipping UEFI support")


# ---------------------------------------------------------------------------
# ext2 partition — delegate to mkext2_demo.py
# ---------------------------------------------------------------------------

def setup_ext2(img_path: str, p2_offset: int, p2_size_mb: int,
               inject_dir: str | None) -> None:
    """Write an ext2 filesystem at p2_offset in img_path.

    Strategy: build a standalone ext2 image in a temp file, then splice
    it into the main image at the right offset with a single Python write.
    This avoids needing loop-mount privileges.
    """
    script = os.path.join(os.path.dirname(__file__), "mkext2_demo.py")
    if not os.path.exists(script):
        print("[~] mkext2_demo.py not found — skipping ext2 partition setup")
        return

    with tempfile.NamedTemporaryFile(suffix=".ext2", delete=False) as tf:
        tmp_path = tf.name

    try:
        cmd = [
            sys.executable, script,
            "--output", tmp_path,
            "--size-mb", str(p2_size_mb),
        ]
        if inject_dir and os.path.isdir(inject_dir):
            cmd += ["--inject-dir", inject_dir]

        print(f"[*] Building ext2 data partition ({p2_size_mb} MiB) …")
        result = subprocess.run(cmd)
        if result.returncode != 0:
            sys.exit("[!] mkext2_demo.py failed")

        # Splice temp image into the main image at partition 2 offset
        print("[*] Splicing ext2 image into disk image …")
        chunk = 4 * MiB
        with open(img_path, "r+b") as dst, open(tmp_path, "rb") as src:
            dst.seek(p2_offset)
            while True:
                data = src.read(chunk)
                if not data:
                    break
                dst.write(data)
    finally:
        os.unlink(tmp_path)


# ---------------------------------------------------------------------------
# Limine BIOS install
# ---------------------------------------------------------------------------

def limine_bios_install(limine_bin: str, img_path: str) -> None:
    print("[*] Installing Limine BIOS bootstrap in MBR …")
    _run(limine_bin, "bios-install", img_path)


# ---------------------------------------------------------------------------
# Limine config — generate USB version with initrd module entry
# ---------------------------------------------------------------------------

def _build_limine_cfg(kernel_cmdline: str = "") -> str:
    lines = [
        "TIMEOUT=3",
        "",
        ":RodNIX",
        "PROTOCOL=multiboot2",
        "KERNEL_PATH=boot:///boot/rodnix.kernel",
    ]
    if kernel_cmdline:
        lines.append(f"KERNEL_CMDLINE={kernel_cmdline}")
    lines += [
        "MODULE_PATH=boot:///boot/initrd.img",
        "",
        ":RodNIX (verbose)",
        "PROTOCOL=multiboot2",
        "KERNEL_PATH=boot:///boot/rodnix.kernel",
        "KERNEL_CMDLINE=bootverbose verbose_sysinit=1 startup_debug=verbose bootlog=verbose",
        "MODULE_PATH=boot:///boot/initrd.img",
    ]
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Create a bootable RodNIX USB disk image"
    )
    ap.add_argument("--kernel",   required=True, help="path to rodnix.kernel")
    ap.add_argument("--initrd",   default="",    help="path to initrd.img (optional)")
    ap.add_argument("--output",   required=True, help="output disk image path")
    ap.add_argument("--size-mb",  type=int, default=512,
                    help="total disk image size in MiB (default: 512)")
    ap.add_argument("--fat-mb",   type=int, default=FAT_PART_SIZE_MiB,
                    help="FAT32 boot partition size in MiB (default: 64)")
    ap.add_argument("--ext2-inject-dir", default="",
                    help="staging directory to inject into ext2 data partition")
    ap.add_argument("--limine-dir", default="",
                    help="path to limine data dir (auto-detected if omitted)")
    ap.add_argument("--kernel-cmdline", default="",
                    help="extra kernel command line arguments")
    args = ap.parse_args()

    # Validate inputs
    if not os.path.exists(args.kernel):
        sys.exit(f"[!] Kernel not found: {args.kernel}")

    total_sectors = args.size_mb * MiB // SECTOR
    fat_sectors   = args.fat_mb  * MiB // SECTOR

    p1_start   = P1_START_LBA
    p1_sectors = fat_sectors
    p2_start   = p1_start + p1_sectors
    p2_sectors = total_sectors - p2_start
    p2_size_mb = p2_sectors * SECTOR // MiB

    p1_offset = p1_start * SECTOR
    p2_offset = p2_start * SECTOR

    if p2_size_mb < 16:
        sys.exit(f"[!] ext2 partition too small ({p2_size_mb} MiB). Increase --size-mb.")

    print(f"[*] Disk image: {args.output} ({args.size_mb} MiB)")
    print(f"    P1 FAT32: LBA {p1_start}+{p1_sectors} = {args.fat_mb} MiB (boot)")
    print(f"    P2 ext2:  LBA {p2_start}+{p2_sectors} = {p2_size_mb} MiB (data)")

    # Find Limine
    limine_bin = shutil.which("limine")
    if limine_bin is None:
        sys.exit(
            "[!] 'limine' binary not found.\n"
            "    macOS:  brew install limine\n"
            "    Debian: apt install limine\n"
            "    Or:     https://limine-bootloader.org/"
        )

    if args.limine_dir:
        limine_dir = args.limine_dir
    else:
        result = subprocess.run([limine_bin, "--print-datadir"],
                                capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit("[!] Could not determine Limine data directory.")
        limine_dir = result.stdout.strip()

    print(f"[*] Limine data dir: {limine_dir}")

    # Create output directory
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    # Allocate raw image
    print("[*] Allocating disk image …")
    with open(args.output, "wb") as f:
        f.seek(args.size_mb * MiB - 1)
        f.write(b'\x00')

    # Write MBR partition table
    write_mbr(args.output, p1_start, p1_sectors, p2_start, p2_sectors)

    # Build and write a temporary limine.cfg for USB
    with tempfile.NamedTemporaryFile(mode="w", suffix=".cfg",
                                     delete=False) as tf:
        tf.write(_build_limine_cfg(args.kernel_cmdline))
        tmp_cfg = tf.name

    try:
        # FAT32 partition
        setup_fat32(
            img_path=args.output,
            p1_offset=p1_offset,
            p1_size_bytes=p1_sectors * SECTOR,
            kernel=args.kernel,
            initrd=args.initrd or None,
            limine_dir=limine_dir,
            cfg_path=tmp_cfg,
        )
    finally:
        os.unlink(tmp_cfg)

    # ext2 partition
    setup_ext2(
        img_path=args.output,
        p2_offset=p2_offset,
        p2_size_mb=p2_size_mb,
        inject_dir=args.ext2_inject_dir or None,
    )

    # Install Limine BIOS bootstrap into MBR boot code area
    limine_bios_install(limine_bin, args.output)

    size_mib = os.path.getsize(args.output) / MiB
    print(f"\n[+] USB image ready: {args.output}  ({size_mib:.0f} MiB)")
    print( "    Flash with:")
    print(f"      dd if={args.output} of=/dev/sdX bs=4M status=progress && sync")
    print( "    (replace /dev/sdX with your USB drive — double-check before writing!)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
