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


def _ext2_inject_dir(img_path: str, staging_dir: str) -> None:
    """
    Inject every file from staging_dir into an existing ext2 image.
    Pure-Python implementation — no host tools required.

    Strategy: open the image, parse superblock/GDT, walk bitmaps to
    find free inodes and blocks, then write dirs + files top-down.
    """
    import math

    EXT2_FT_DIR  = 2
    EXT2_FT_REG  = 1
    EXT2_S_IFDIR = 0x4000
    EXT2_S_IFREG = 0x8000

    def blk_offset(b: int) -> int:
        return b * BLOCK_SIZE

    with open(img_path, "r+b") as f:
        # ── Parse superblock ─────────────────────────────────
        f.seek(BLOCK_SIZE)
        raw_sb = f.read(1024)

        inodes_count    = struct.unpack_from("<I", raw_sb, 0x00)[0]
        blocks_count    = struct.unpack_from("<I", raw_sb, 0x04)[0]
        first_data_blk  = struct.unpack_from("<I", raw_sb, 0x14)[0]
        log_block_size  = struct.unpack_from("<I", raw_sb, 0x18)[0]
        blks_per_group  = struct.unpack_from("<I", raw_sb, 0x20)[0]
        inos_per_group  = struct.unpack_from("<I", raw_sb, 0x28)[0]
        inode_size_sb   = struct.unpack_from("<H", raw_sb, 0x58)[0]
        rev_level       = struct.unpack_from("<I", raw_sb, 0x4C)[0]

        bsz   = 1024 << log_block_size
        isz   = inode_size_sb if rev_level > 0 else 128
        ngrps = math.ceil(blocks_count / blks_per_group)

        # ── Parse GDT ────────────────────────────────────────
        gdt_blk = first_data_blk + 1
        f.seek(blk_offset(gdt_blk))
        raw_gdt = f.read(ngrps * 32)

        def gdt_field(grp: int, off: int, fmt: str):
            return struct.unpack_from(fmt, raw_gdt, grp * 32 + off)[0]

        def write_gdt_field(grp: int, off: int, fmt: str, val) -> None:
            pos = blk_offset(gdt_blk) + grp * 32 + off
            f.seek(pos)
            f.write(struct.pack(fmt, val))

        # Reload superblock free counts later
        def sb_free_inodes() -> int:
            f.seek(BLOCK_SIZE + 0x10)
            return struct.unpack("<I", f.read(4))[0]

        def set_sb_free_inodes(n: int) -> None:
            f.seek(BLOCK_SIZE + 0x10)
            f.write(struct.pack("<I", n))

        def sb_free_blocks() -> int:
            f.seek(BLOCK_SIZE + 0x0C)
            return struct.unpack("<I", f.read(4))[0]

        def set_sb_free_blocks(n: int) -> None:
            f.seek(BLOCK_SIZE + 0x0C)
            f.write(struct.pack("<I", n))

        # ── Bitmap helpers ───────────────────────────────────
        def bitmap_find_free(bitmap_blk: int, count: int) -> int:
            """Return 0-based index of first free bit, or -1."""
            f.seek(blk_offset(bitmap_blk))
            bmap = bytearray(f.read(bsz))
            for i in range(count):
                if not (bmap[i // 8] & (1 << (i % 8))):
                    return i
            return -1

        def bitmap_set(bitmap_blk: int, idx: int) -> None:
            byte_off = blk_offset(bitmap_blk) + idx // 8
            f.seek(byte_off)
            b = struct.unpack("B", f.read(1))[0]
            b |= (1 << (idx % 8))
            f.seek(byte_off)
            f.write(struct.pack("B", b))

        # ── Allocate inode ───────────────────────────────────
        def alloc_inode() -> int:
            for g in range(ngrps):
                imap_blk = gdt_field(g, 0x04, "<I")
                free_inos = gdt_field(g, 0x0E, "<H")
                if free_inos == 0:
                    continue
                idx = bitmap_find_free(imap_blk, inos_per_group)
                if idx < 0:
                    continue
                bitmap_set(imap_blk, idx)
                write_gdt_field(g, 0x0E, "<H", free_inos - 1)
                set_sb_free_inodes(max(0, sb_free_inodes() - 1))
                ino_num = g * inos_per_group + idx + 1  # 1-based
                return ino_num
            raise OSError("ext2: no free inodes")

        # ── Allocate block ───────────────────────────────────
        def alloc_block() -> int:
            for g in range(ngrps):
                bmap_blk = gdt_field(g, 0x00, "<I")
                free_blks = gdt_field(g, 0x0C, "<H")
                if free_blks == 0:
                    continue
                idx = bitmap_find_free(bmap_blk, blks_per_group)
                if idx < 0:
                    continue
                bitmap_set(bmap_blk, idx)
                write_gdt_field(g, 0x0C, "<H", free_blks - 1)
                set_sb_free_blocks(max(0, sb_free_blocks() - 1))
                blk_num = g * blks_per_group + idx
                return blk_num
            raise OSError("ext2: no free blocks")

        # ── Read/write inode ─────────────────────────────────
        def inode_offset(ino_num: int) -> int:
            g = (ino_num - 1) // inos_per_group
            idx = (ino_num - 1) % inos_per_group
            itab_blk = gdt_field(g, 0x08, "<I")
            return blk_offset(itab_blk) + idx * isz

        def read_inode_raw(ino_num: int) -> bytearray:
            f.seek(inode_offset(ino_num))
            return bytearray(f.read(isz))

        def write_inode_raw(ino_num: int, data: bytes) -> None:
            f.seek(inode_offset(ino_num))
            f.write(data[:isz].ljust(isz, b"\x00"))

        def make_inode(mode: int, size: int, block0: int,
                       links: int = 1, blocks_512: int = 0) -> bytes:
            if blocks_512 == 0:
                blocks_512 = (size + 511) // 512
            data = struct.pack(
                "<HHIIIIIHHIII",
                mode, 0, size,        # i_mode, uid, size
                0, 0, 0, 0,           # atime,ctime,mtime,dtime
                0, links,             # gid, links_count
                blocks_512,           # i_blocks (512-byte units)
                0, 0,                 # flags, osd1
            )
            blks = [0] * 15
            blks[0] = block0
            data += struct.pack("<15I", *blks)
            data += struct.pack("<IIII", 0, 0, 0, 0)
            data += b"\x00" * 12
            return data[:isz].ljust(isz, b"\x00")

        # ── Directory block helpers ───────────────────────────
        def build_dir_block(entries: list) -> bytes:
            """entries = [(ino, name_bytes, ftype), ...]"""
            buf = bytearray(bsz)
            pos = 0
            for i, (ino, name, ftype) in enumerate(entries):
                nlen = len(name)
                is_last = (i == len(entries) - 1)
                reclen = bsz - pos if is_last else align4(8 + nlen)
                struct.pack_into("<IHBB", buf, pos, ino, reclen, nlen, ftype)
                buf[pos + 8: pos + 8 + nlen] = name
                pos += reclen
            return bytes(buf)

        def dir_append_entry(dir_ino: int, child_ino: int,
                             name: bytes, ftype: int) -> None:
            """Append an entry to an existing directory inode."""
            raw = read_inode_raw(dir_ino)
            blks = struct.unpack_from("<15I", raw, 40)
            blk0 = blks[0]
            f.seek(blk_offset(blk0))
            dir_data = bytearray(f.read(bsz))

            # Walk entries to find the last one and shrink it
            pos = 0
            last_pos = 0
            while pos < bsz:
                rec_ino   = struct.unpack_from("<I", dir_data, pos)[0]
                rec_len   = struct.unpack_from("<H", dir_data, pos + 4)[0]
                rec_nlen  = dir_data[pos + 6]
                if rec_len == 0:
                    break
                last_pos = pos
                pos += rec_len

            # Compact last entry and append new one
            last_rec_len = struct.unpack_from("<H", dir_data, last_pos)[0]
            last_nlen    = dir_data[last_pos + 6]
            actual_last  = align4(8 + last_nlen)
            remaining    = last_rec_len - actual_last

            nlen = len(name)
            needed = align4(8 + nlen)
            if remaining < needed:
                raise OSError(f"ext2: no space in dir block for '{name.decode()}'")

            # Shrink last entry
            struct.pack_into("<H", dir_data, last_pos + 4, actual_last)
            # Write new entry at end
            new_pos = last_pos + actual_last
            struct.pack_into("<IHBB", dir_data, new_pos,
                             child_ino, remaining, nlen, ftype)
            dir_data[new_pos + 8: new_pos + 8 + nlen] = name

            f.seek(blk_offset(blk0))
            f.write(bytes(dir_data))

        # ── Lookup existing inode by path ─────────────────────
        def lookup_path(path: str) -> int:
            """Return inode number for path, or 0 if not found."""
            parts = [p for p in path.split("/") if p]
            cur_ino = 2  # root
            for part in parts:
                raw = read_inode_raw(cur_ino)
                blks = struct.unpack_from("<15I", raw, 40)
                blk0 = blks[0]
                if blk0 == 0:
                    return 0
                f.seek(blk_offset(blk0))
                dir_data = f.read(bsz)
                pos = 0
                found = 0
                while pos < bsz:
                    e_ino  = struct.unpack_from("<I", dir_data, pos)[0]
                    e_len  = struct.unpack_from("<H", dir_data, pos + 4)[0]
                    e_nlen = dir_data[pos + 6]
                    if e_len == 0:
                        break
                    e_name = dir_data[pos + 8: pos + 8 + e_nlen]
                    if e_name == part.encode():
                        found = e_ino
                        break
                    pos += e_len
                if not found:
                    return 0
                cur_ino = found
            return cur_ino

        # ── Create directory in parent ────────────────────────
        def mkdir_in(parent_ino: int, name: str) -> int:
            new_ino = alloc_inode()
            new_blk = alloc_block()
            dir_data = build_dir_block([
                (new_ino, b".",  EXT2_FT_DIR),
                (parent_ino, b"..", EXT2_FT_DIR),
            ])
            f.seek(blk_offset(new_blk))
            f.write(dir_data)
            mode = EXT2_S_IFDIR | 0o755
            write_inode_raw(new_ino, make_inode(mode, bsz, new_blk, links=2,
                                                blocks_512=bsz // 512))
            dir_append_entry(parent_ino, new_ino, name.encode(), EXT2_FT_DIR)
            return new_ino

        # ── Ensure directory path exists ──────────────────────
        def ensure_dir(path: str) -> int:
            """Create intermediate dirs as needed; return final inode."""
            parts = [p for p in path.split("/") if p]
            cur_ino = 2
            cur_path = ""
            for part in parts:
                cur_path += "/" + part
                existing = lookup_path(cur_path)
                if existing:
                    cur_ino = existing
                else:
                    cur_ino = mkdir_in(cur_ino, part)
            return cur_ino

        # ── Write a regular file ──────────────────────────────
        def write_file(parent_ino: int, name: str, data: bytes) -> int:
            new_ino  = alloc_inode()
            n_blocks = (len(data) + bsz - 1) // bsz if data else 0

            # For simplicity support only direct blocks (12 × bsz = 12 KiB max per block).
            # Files up to 12 * 1024 = 12 KiB fit in direct blocks.
            # For larger files we use the single indirect block (block[12]).
            MAX_DIRECT = 12
            block_nums: list[int] = []
            for _ in range(n_blocks):
                block_nums.append(alloc_block())

            # Write file data
            for i, blk in enumerate(block_nums):
                chunk = data[i * bsz: (i + 1) * bsz].ljust(bsz, b"\x00")
                f.seek(blk_offset(blk))
                f.write(chunk)

            # Build inode with direct + optional indirect block
            raw_ino = bytearray(isz)
            mode = EXT2_S_IFREG | 0o755
            blocks_512 = n_blocks * (bsz // 512)
            struct.pack_into(
                "<HHIIIIIHHIII",
                raw_ino, 0,
                mode, 0, len(data),
                0, 0, 0, 0,
                0, 1,
                blocks_512,
                0, 0,
            )
            # Direct blocks
            for i, bn in enumerate(block_nums[:MAX_DIRECT]):
                struct.pack_into("<I", raw_ino, 40 + i * 4, bn)
            # Single indirect block if needed
            if len(block_nums) > MAX_DIRECT:
                ind_blk = alloc_block()
                ind_data = bytearray(bsz)
                for i, bn in enumerate(block_nums[MAX_DIRECT:]):
                    struct.pack_into("<I", ind_data, i * 4, bn)
                f.seek(blk_offset(ind_blk))
                f.write(bytes(ind_data))
                struct.pack_into("<I", raw_ino, 40 + 12 * 4, ind_blk)
                blocks_512 += bsz // 512
                struct.pack_into("<I", raw_ino, 28, blocks_512)  # update i_blocks

            write_inode_raw(new_ino, bytes(raw_ino))
            dir_append_entry(parent_ino, new_ino, name.encode(), EXT2_FT_REG)
            return new_ino

        # ── Walk staging dir and inject ───────────────────────
        staging = staging_dir.rstrip("/")
        for dirpath, dirnames, filenames in os.walk(staging):
            dirnames.sort()
            rel_dir = dirpath[len(staging):]
            if rel_dir == "":
                rel_dir = "/"
            parent_ino = ensure_dir(rel_dir)
            for fname in sorted(filenames):
                fpath = os.path.join(dirpath, fname)
                with open(fpath, "rb") as fh:
                    fdata = fh.read()
                write_file(parent_ino, fname, fdata)
                rel_path = rel_dir.rstrip("/") + "/" + fname
                print(f"  inject: {rel_path} ({len(fdata)} bytes)")


def main() -> int:
    ap = argparse.ArgumentParser(description="Create demo ext2 disk image for RodNIX")
    ap.add_argument("--output", required=True, help="disk image path")
    ap.add_argument("--size-mb", type=int, default=128, help="image size in MiB")
    ap.add_argument("--inject-dir", default="",
                    help="staging directory to inject into image after creation")
    args = ap.parse_args()
    build_image(args.output, args.size_mb)
    if args.inject_dir and os.path.isdir(args.inject_dir):
        print(f"[ext2] Injecting from {args.inject_dir} ...")
        _ext2_inject_dir(args.output, args.inject_dir)
        print("[ext2] Injection complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
