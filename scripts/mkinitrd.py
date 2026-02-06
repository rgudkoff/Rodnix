#!/usr/bin/env python3

import os
import struct
import sys

MAGIC = 0x52444E58  # 'RDNX'


def collect_files(root):
    entries = []
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            rel = "/" + rel.replace(os.sep, "/")
            entries.append((rel, full))
    entries.sort(key=lambda x: x[0])
    return entries


def main():
    if len(sys.argv) != 3:
        print("usage: mkinitrd.py <rootfs_dir> <out_file>")
        return 1

    root = sys.argv[1]
    out_path = sys.argv[2]
    if not os.path.isdir(root):
        print(f"rootfs dir not found: {root}")
        return 1

    entries = collect_files(root)
    header = struct.pack("<II", MAGIC, len(entries))

    table = bytearray()
    data = bytearray()
    offset = 8 + len(entries) * (64 + 4 + 4)

    for rel, full in entries:
        with open(full, "rb") as f:
            content = f.read()
        path_bytes = rel.encode("ascii", errors="ignore")[:63]
        path_bytes = path_bytes + b"\x00" * (64 - len(path_bytes))
        table += path_bytes
        table += struct.pack("<II", offset, len(content))
        data += content
        offset += len(content)

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(table)
        f.write(data)

    return 0


if __name__ == "__main__":
    sys.exit(main())
