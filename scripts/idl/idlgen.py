#!/usr/bin/env python3
"""
Minimal IDL generator for RodNIX.

Grammar (very small subset):
  interface <name> {
    rpc <name>(arg: type, ...) -> (ret: type, ...);
  }

Types: u32, u64, string, port

Usage:
  idlgen.py <input.defs> <out_dir>

Generates placeholder client/server stub C files with signatures.
"""

import os
import re
import sys
from dataclasses import dataclass
from typing import List


@dataclass
class Field:
    name: str
    type: str


@dataclass
class Rpc:
    name: str
    args: List[Field]
    rets: List[Field]


@dataclass
class Interface:
    name: str
    rpcs: List[Rpc]


TYPE_MAP = {
    "u32": "uint32_t",
    "u64": "uint64_t",
    "string": "const char*",
    "port": "uint64_t",
}


def parse_fields(blob: str) -> List[Field]:
    blob = blob.strip()
    if not blob:
        return []
    parts = [p.strip() for p in blob.split(",") if p.strip()]
    fields: List[Field] = []
    for part in parts:
        if ":" not in part:
            raise ValueError(f"bad field: {part}")
        name, ftype = [x.strip() for x in part.split(":", 1)]
        if ftype not in TYPE_MAP:
            raise ValueError(f"unknown type: {ftype}")
        fields.append(Field(name=name, type=ftype))
    return fields


def parse_defs(text: str) -> Interface:
    m = re.search(r"interface\s+(\w+)\s*\{([\s\S]*)\}", text)
    if not m:
        raise ValueError("missing interface block")
    name = m.group(1)
    body = m.group(2)
    rpcs: List[Rpc] = []
    for rpc_m in re.finditer(r"rpc\s+(\w+)\s*\(([^)]*)\)\s*->\s*\(([^)]*)\)\s*;", body):
        rpc_name = rpc_m.group(1)
        args = parse_fields(rpc_m.group(2))
        rets = parse_fields(rpc_m.group(3))
        rpcs.append(Rpc(name=rpc_name, args=args, rets=rets))
    if not rpcs:
        raise ValueError("no rpc definitions found")
    return Interface(name=name, rpcs=rpcs)


def c_type(ftype: str) -> str:
    return TYPE_MAP[ftype]


def gen_client(iface: Interface) -> str:
    lines = []
    lines.append("#include <stdint.h>")
    lines.append("\n/* Auto-generated client stubs. */\n")
    for rpc in iface.rpcs:
        args = [f"{c_type(f.type)} {f.name}" for f in rpc.args]
        ret_struct = "void"
        if rpc.rets:
            ret_struct = f"{iface.name}_{rpc.name}_reply_t"
        lines.append(f"typedef struct {iface.name}_{rpc.name}_reply {{")
        for f in rpc.rets:
            lines.append(f"    {c_type(f.type)} {f.name};")
        lines.append(f"}} {iface.name}_{rpc.name}_reply_t;\n")
        lines.append(f"int {iface.name}_{rpc.name}_call({', '.join(args)}{', ' if args else ''}{ret_struct}* out);\n")
    return "\n".join(lines)


def gen_server(iface: Interface) -> str:
    lines = []
    lines.append("#include <stdint.h>")
    lines.append("\n/* Auto-generated server dispatch prototypes. */\n")
    for rpc in iface.rpcs:
        args = [f"{c_type(f.type)} {f.name}" for f in rpc.args]
        for f in rpc.rets:
            args.append(f"{c_type(f.type)}* out_{f.name}")
        lines.append(f"int {iface.name}_{rpc.name}_impl({', '.join(args)});\n")
    lines.append(f"int {iface.name}_dispatch(uint32_t msg_id, void* msg, void* reply);\n")
    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: idlgen.py <input.defs> <out_dir>")
        return 1

    defs_path = sys.argv[1]
    out_dir = sys.argv[2]

    if not os.path.isfile(defs_path):
        print(f"idlgen: input not found: {defs_path}")
        return 1

    with open(defs_path, "r", encoding="utf-8") as f:
        text = f.read()

    try:
        iface = parse_defs(text)
    except ValueError as e:
        print(f"idlgen: parse error: {e}")
        return 1

    os.makedirs(out_dir, exist_ok=True)

    base = os.path.splitext(os.path.basename(defs_path))[0]
    client = os.path.join(out_dir, f"{base}_client.h")
    server = os.path.join(out_dir, f"{base}_server.h")

    with open(client, "w", encoding="utf-8") as f:
        f.write(gen_client(iface))
    with open(server, "w", encoding="utf-8") as f:
        f.write(gen_server(iface))

    print(f"idlgen: generated {client} and {server}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
