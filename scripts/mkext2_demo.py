#!/usr/bin/env python3
"""
Build a tiny ext2 image suitable for RodNIX read-only ext2 driver tests.
"""

from __future__ import annotations

import argparse
import os
import struct


BLOCK_SIZE = 1024
INODE_SIZE = 128
INODES_PER_GROUP = 128

EXT2_S_IFDIR = 0x4000
EXT2_S_IFREG = 0x8000
EXT2_S_IFMODE_755 = 0o755
EXT2_S_IFMODE_644 = 0o644

ROOT_INO = 2
HELLO_INO = 12
README_INO = 13
DOCS_INO = 14
INFO_INO = 15


def align4(n: int) -> int:
    return (n + 3) & ~3


def set_bit(bitmap: bytearray, idx: int) -> None:
    bitmap[idx // 8] |= 1 << (idx % 8)


def dirent(ino: int, name: bytes, ftype: int, reclen: int | None = None) -> bytes:
    nlen = len(name)
    min_len = align4(8 + nlen)
    if reclen is None:
        reclen = min_len
    return struct.pack("<IHBB", ino, reclen, nlen, ftype) + name + b"\x00" * (reclen - 8 - nlen)


def inode_pack(mode: int, size: int, block0: int, links: int = 1) -> bytes:
    # ext2 inode (128 bytes)
    # blocks field is number of 512-byte sectors
    blocks_512 = ((size + 511) // 512)
    fields = [
        mode,              # i_mode (H)
        0,                 # i_uid (H)
        size,              # i_size (I)
        0, 0, 0, 0,        # atime, ctime, mtime, dtime
        0,                 # i_gid (H)
        links,             # i_links_count (H)
        blocks_512,        # i_blocks (I)
        0,                 # i_flags (I)
        0,                 # i_osd1 (I)
    ]
    out = struct.pack("<HHIIIIIHHIII", *fields)
    blocks = [0] * 15
    blocks[0] = block0
    out += struct.pack("<15I", *blocks)
    out += struct.pack("<IIII", 0, 0, 0, 0)  # generation, file_acl, dir_acl, faddr
    out += b"\x00" * 12  # osd2
    return out[:INODE_SIZE].ljust(INODE_SIZE, b"\x00")


def build_image(path: str, size_mb: int) -> None:
    size_bytes = size_mb * 1024 * 1024
    total_blocks = size_bytes // BLOCK_SIZE
    if total_blocks < 4096:
        raise ValueError("image too small; use at least 4MB")

    # Layout (single block group)
    sb_block = 1
    gdt_block = 2
    bmap_block = 3
    imap_block = 4
    inode_table_block = 5
    inode_table_blocks = (INODES_PER_GROUP * INODE_SIZE) // BLOCK_SIZE  # 16
    data_start = inode_table_block + inode_table_blocks
    root_block = data_start + 0
    hello_block = data_start + 1
    readme_block = data_start + 2
    docs_block = data_start + 3
    info_block = data_start + 4
    used_upto = info_block

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.truncate(size_bytes)

    with open(path, "r+b") as f:
        # Superblock @ 1024
        free_blocks = total_blocks - (used_upto + 1)
        free_inodes = INODES_PER_GROUP - 15  # reserve up to inode 15
        sb = bytearray(1024)
        struct.pack_into("<I", sb, 0x00, INODES_PER_GROUP)   # inodes_count
        struct.pack_into("<I", sb, 0x04, total_blocks)       # blocks_count
        struct.pack_into("<I", sb, 0x0C, free_blocks)        # free_blocks_count
        struct.pack_into("<I", sb, 0x10, free_inodes)        # free_inodes_count
        struct.pack_into("<I", sb, 0x14, sb_block)           # first_data_block
        struct.pack_into("<I", sb, 0x18, 0)                  # log_block_size=1024
        struct.pack_into("<I", sb, 0x20, total_blocks - 1)   # blocks_per_group
        struct.pack_into("<I", sb, 0x28, INODES_PER_GROUP)   # inodes_per_group
        struct.pack_into("<H", sb, 0x38, 0xEF53)             # magic
        struct.pack_into("<I", sb, 0x4C, 1)                  # rev_level
        struct.pack_into("<I", sb, 0x54, 11)                 # first_ino
        struct.pack_into("<H", sb, 0x58, INODE_SIZE)         # inode_size
        struct.pack_into("<I", sb, 0x5C, 0)                  # bg nr
        struct.pack_into("<I", sb, 0x60, 0)                  # feature_compat
        struct.pack_into("<I", sb, 0x64, 0x0002)             # feature_incompat: filetype
        struct.pack_into("<I", sb, 0x68, 0)                  # feature_ro_compat
        f.seek(BLOCK_SIZE)
        f.write(sb)

        # Group descriptor table @ block 2
        gdt = bytearray(BLOCK_SIZE)
        struct.pack_into("<I", gdt, 0x00, bmap_block)
        struct.pack_into("<I", gdt, 0x04, imap_block)
        struct.pack_into("<I", gdt, 0x08, inode_table_block)
        struct.pack_into("<H", gdt, 0x0C, free_blocks & 0xFFFF)
        struct.pack_into("<H", gdt, 0x0E, free_inodes & 0xFFFF)
        struct.pack_into("<H", gdt, 0x10, 2)  # root + docs dirs
        f.seek(gdt_block * BLOCK_SIZE)
        f.write(gdt)

        # Block bitmap
        bmap = bytearray(BLOCK_SIZE)
        for b in range(0, used_upto + 1):
            set_bit(bmap, b)
        f.seek(bmap_block * BLOCK_SIZE)
        f.write(bmap)

        # Inode bitmap
        imap = bytearray(BLOCK_SIZE)
        for i in range(1, 16):  # mark inodes 1..15 used
            set_bit(imap, i - 1)
        f.seek(imap_block * BLOCK_SIZE)
        f.write(imap)

        # Inode table
        itab = bytearray(inode_table_blocks * BLOCK_SIZE)
        def put_inode(ino: int, data: bytes) -> None:
            off = (ino - 1) * INODE_SIZE
            itab[off:off + INODE_SIZE] = data[:INODE_SIZE]

        hello_txt = b"Hello from RodNIX ext2 disk\n"
        readme_txt = (
            b"RodNIX ext2 demo volume\n"
            b"- mounted from disk0 to /mnt\n"
            b"- files are served by kernel ext2 read-only path\n"
        )
        info_txt = b"/mnt/docs/info.txt reached via directory traversal\n"

        put_inode(ROOT_INO, inode_pack(EXT2_S_IFDIR | EXT2_S_IFMODE_755, BLOCK_SIZE, root_block, links=3))
        put_inode(HELLO_INO, inode_pack(EXT2_S_IFREG | EXT2_S_IFMODE_644, len(hello_txt), hello_block))
        put_inode(README_INO, inode_pack(EXT2_S_IFREG | EXT2_S_IFMODE_644, len(readme_txt), readme_block))
        put_inode(DOCS_INO, inode_pack(EXT2_S_IFDIR | EXT2_S_IFMODE_755, BLOCK_SIZE, docs_block, links=2))
        put_inode(INFO_INO, inode_pack(EXT2_S_IFREG | EXT2_S_IFMODE_644, len(info_txt), info_block))

        f.seek(inode_table_block * BLOCK_SIZE)
        f.write(itab)

        # Root directory block
        root_dir = bytearray(BLOCK_SIZE)
        entries = [
            dirent(ROOT_INO, b".", 2),
            dirent(ROOT_INO, b"..", 2),
            dirent(HELLO_INO, b"hello.txt", 1),
            dirent(README_INO, b"README.txt", 1),
        ]
        pos = 0
        for e in entries:
            root_dir[pos:pos + len(e)] = e
            pos += len(e)
        # Last entry consumes remaining bytes
        last = dirent(DOCS_INO, b"docs", 2, BLOCK_SIZE - pos)
        root_dir[pos:pos + len(last)] = last
        f.seek(root_block * BLOCK_SIZE)
        f.write(root_dir)

        # docs directory block
        docs_dir = bytearray(BLOCK_SIZE)
        e0 = dirent(DOCS_INO, b".", 2)
        e1 = dirent(ROOT_INO, b"..", 2)
        e2 = dirent(INFO_INO, b"info.txt", 1, BLOCK_SIZE - len(e0) - len(e1))
        docs_dir[0:len(e0)] = e0
        docs_dir[len(e0):len(e0) + len(e1)] = e1
        docs_dir[len(e0) + len(e1):len(e0) + len(e1) + len(e2)] = e2
        f.seek(docs_block * BLOCK_SIZE)
        f.write(docs_dir)

        # file data
        def write_block(blk: int, data: bytes) -> None:
            buf = bytearray(BLOCK_SIZE)
            buf[:len(data)] = data
            f.seek(blk * BLOCK_SIZE)
            f.write(buf)

        write_block(hello_block, hello_txt)
        write_block(readme_block, readme_txt)
        write_block(info_block, info_txt)


def main() -> int:
    ap = argparse.ArgumentParser(description="Create demo ext2 disk image for RodNIX")
    ap.add_argument("--output", required=True, help="disk image path")
    ap.add_argument("--size-mb", type=int, default=128, help="image size in MiB")
    args = ap.parse_args()
    build_image(args.output, args.size_mb)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
