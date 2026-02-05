#!/usr/bin/env python3
"""
Minimal IDL generator stub for RodNIX.

Usage:
  idlgen.py <input.defs> <out_dir>

This tool currently validates that the file exists and writes
placeholder client/server stub files. It does not generate
real code yet.
"""

import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: idlgen.py <input.defs> <out_dir>")
        return 1

    defs_path = sys.argv[1]
    out_dir = sys.argv[2]

    if not os.path.isfile(defs_path):
        print(f"idlgen: input not found: {defs_path}")
        return 1

    os.makedirs(out_dir, exist_ok=True)

    base = os.path.splitext(os.path.basename(defs_path))[0]
    client = os.path.join(out_dir, f"{base}_client.c")
    server = os.path.join(out_dir, f"{base}_server.c")

    for path, tag in ((client, "client"), (server, "server")):
        with open(path, "w", encoding="utf-8") as f:
            f.write("/*\n")
            f.write(" * Auto-generated stub (placeholder).\n")
            f.write(" * TODO: implement real IDL codegen.\n")
            f.write(" */\n\n")
            f.write(f"/* source: {defs_path} */\n")
            f.write(f"/* kind: {tag} */\n")

    print(f"idlgen: generated placeholders in {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
