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

Generates C header stubs for client/server and IPC message ids.
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
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("#include \"ipc.h\"")
    lines.append("#include \"idl_runtime.h\"")
    lines.append(f"#include \"{iface.name}_ipc.h\"")
    lines.append("\n/* Auto-generated client stubs. */\n")
    for rpc in iface.rpcs:
        req = f"{iface.name}_{rpc.name}_request_t"
        rep = f"{iface.name}_{rpc.name}_reply_t"
        lines.append(f"int {iface.name}_{rpc.name}_call(port_t* port, const {req}* req, {rep}* out);\n")
    return "\n".join(lines)


def gen_server(iface: Interface) -> str:
    lines = []
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("#include \"ipc.h\"")
    lines.append("#include \"idl_runtime.h\"")
    lines.append(f"#include \"{iface.name}_ipc.h\"")
    lines.append("\n/* Auto-generated server dispatch prototypes. */\n")
    for rpc in iface.rpcs:
        req = f"{iface.name}_{rpc.name}_request_t"
        rep = f"{iface.name}_{rpc.name}_reply_t"
        lines.append(f"int {iface.name}_{rpc.name}_impl(const {req}* req, {rep}* out);\n")
    lines.append(f"int {iface.name}_dispatch(const ipc_message_t* msg);\n")
    return "\n".join(lines)


def gen_ipc(iface: Interface) -> str:
    lines = []
    lines.append("#ifndef _RODNIX_IDL_IPC_H")
    lines.append("#define _RODNIX_IDL_IPC_H\n")
    lines.append("#include <stdint.h>\n")
    lines.append("/* Auto-generated IPC message ids. */")
    lines.append("enum {")
    for i, rpc in enumerate(iface.rpcs, start=1):
        lines.append(f"    {iface.name.upper()}_MSG_{rpc.name.upper()} = {i},")
    lines.append("};\n")
    lines.append("/* Auto-generated request/reply structs. */")
    for rpc in iface.rpcs:
        lines.append(f"typedef struct {iface.name}_{rpc.name}_request {{")
        if rpc.args:
            for f in rpc.args:
                lines.append(f"    {c_type(f.type)} {f.name};")
        else:
            lines.append("    uint32_t _unused;")
        lines.append(f"}} {iface.name}_{rpc.name}_request_t;\n")

        lines.append(f"typedef struct {iface.name}_{rpc.name}_reply {{")
        if rpc.rets:
            for f in rpc.rets:
                lines.append(f"    {c_type(f.type)} {f.name};")
        else:
            lines.append("    uint32_t _unused;")
        lines.append(f"}} {iface.name}_{rpc.name}_reply_t;\n")
    lines.append("#endif /* _RODNIX_IDL_IPC_H */")
    return "\n".join(lines)


def gen_client_c(iface: Interface, base: str) -> str:
    lines = []
    lines.append("#include <stdint.h>")
    lines.append("#include \"ipc.h\"")
    lines.append("#include \"idl_runtime.h\"")
    lines.append(f"#include \"{base}_ipc.h\"")
    lines.append(f"#include \"{base}_client.h\"")
    lines.append("")
    for rpc in iface.rpcs:
        req = f"{iface.name}_{rpc.name}_request_t"
        rep = f"{iface.name}_{rpc.name}_reply_t"
        lines.append(f"int {iface.name}_{rpc.name}_call(port_t* port, const {req}* req, {rep}* out)")
        lines.append("{")
        lines.append(f"    return idl_ipc_call(port, {iface.name.upper()}_MSG_{rpc.name.upper()}, req, sizeof({req}), out, sizeof({rep}), 0);")
        lines.append("}\n")
    return "\n".join(lines)


def gen_server_c(iface: Interface, base: str) -> str:
    lines = []
    lines.append("#include <stdint.h>")
    lines.append("#include \"ipc.h\"")
    lines.append("#include \"idl_runtime.h\"")
    lines.append("#include \"common.h\"")
    lines.append(f"#include \"{base}_ipc.h\"")
    lines.append(f"#include \"{base}_server.h\"")
    lines.append("")
    lines.append(f"int {iface.name}_dispatch(const ipc_message_t* msg)")
    lines.append("{")
    lines.append("    if (!msg || !msg->data) {")
    lines.append("        return -1;")
    lines.append("    }")
    lines.append("    switch (msg->msg_id) {")
    for rpc in iface.rpcs:
        req = f"{iface.name}_{rpc.name}_request_t"
        rep = f"{iface.name}_{rpc.name}_reply_t"
        lines.append(f"    case {iface.name.upper()}_MSG_{rpc.name.upper()}: {{")
        lines.append(f"        if (msg->msg_size < sizeof({req})) {{")
        lines.append("            return -1;")
        lines.append("        }")
        lines.append(f"        const {req}* req = (const {req}*)msg->data;")
        lines.append(f"        {rep} reply;")
        lines.append("        memset(&reply, 0, sizeof(reply));")
        lines.append(f"        if ({iface.name}_{rpc.name}_impl(req, &reply) != 0) {{")
        lines.append("            return -1;")
        lines.append("        }")
        lines.append("        if (!msg->reply_port) {")
        lines.append("            return -1;")
        lines.append("        }")
        lines.append(f"        return idl_ipc_reply(msg->reply_port, {iface.name.upper()}_MSG_{rpc.name.upper()}, &reply, sizeof({rep}), 0);")
        lines.append("    }")
    lines.append("    default:")
    lines.append("        return -1;")
    lines.append("    }")
    lines.append("}\n")
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
    ipc = os.path.join(out_dir, f"{base}_ipc.h")
    client = os.path.join(out_dir, f"{base}_client.h")
    server = os.path.join(out_dir, f"{base}_server.h")
    client_c = os.path.join(out_dir, f"{base}_client.c")
    server_c = os.path.join(out_dir, f"{base}_server.c")

    with open(ipc, "w", encoding="utf-8") as f:
        f.write(gen_ipc(iface))
    with open(client, "w", encoding="utf-8") as f:
        f.write(gen_client(iface))
    with open(server, "w", encoding="utf-8") as f:
        f.write(gen_server(iface))
    with open(client_c, "w", encoding="utf-8") as f:
        f.write(gen_client_c(iface, base))
    with open(server_c, "w", encoding="utf-8") as f:
        f.write(gen_server_c(iface, base))

    print(f"idlgen: generated {ipc}, {client}, {server}, {client_c}, {server_c}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
