#!/usr/bin/env python3
"""
Create a minimal RodNIX .kmod image header.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

MAGIC = b"RDKMOD1"
HEADER_FMT = "<8s32s16s16sIIII"


def zfield(value: str, size: int) -> bytes:
    raw = value.encode("ascii", errors="strict")
    if len(raw) >= size:
        raise ValueError(f"field too long (max {size - 1}): {value}")
    return raw + b"\x00" * (size - len(raw))


def main() -> int:
    ap = argparse.ArgumentParser(description="Build minimal RodNIX .kmod image")
    ap.add_argument("--output", required=True, help="Output .kmod path")
    ap.add_argument("--name", required=True, help="Module name")
    ap.add_argument("--kind", default="misc", help="Module kind")
    ap.add_argument("--version", default="0.1", help="Module version")
    ap.add_argument("--flags", type=lambda x: int(x, 0), default=0, help="Flags (dec/hex)")
    args = ap.parse_args()

    hdr_size = struct.calcsize(HEADER_FMT)
    image = struct.pack(
        HEADER_FMT,
        MAGIC + b"\x00",
        zfield(args.name, 32),
        zfield(args.kind, 16),
        zfield(args.version, 16),
        args.flags & 0xFFFFFFFF,
        hdr_size,
        0,
        0,
    )

    out = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(image)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"mkkmod: {e}", file=sys.stderr)
        raise
