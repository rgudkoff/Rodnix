#!/usr/bin/env python3
"""
Check that selected userland ABI constants stay aligned with FreeBSD vendor headers.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys


DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$")


def parse_defines(path: Path) -> dict[str, str]:
    defines: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        m = DEFINE_RE.match(raw)
        if not m:
            continue
        name, expr = m.group(1), m.group(2)
        expr = expr.split("/*", 1)[0].split("//", 1)[0].strip()
        if expr:
            defines[name] = expr
    return defines


def parse_c_int(expr: str) -> int:
    s = expr.strip()
    neg = s.startswith("-")
    if neg:
        s = s[1:].strip()

    if s.startswith(("0x", "0X")):
        value = int(s, 16)
    elif s.startswith(("0b", "0B")):
        value = int(s, 2)
    elif len(s) > 1 and s[0] == "0" and s.isdigit():
        value = int(s, 8)
    else:
        value = int(s, 10)
    return -value if neg else value


def resolve_numeric(name: str, table: dict[str, str], seen: set[str] | None = None) -> int:
    if seen is None:
        seen = set()
    if name in seen:
        raise ValueError(f"cyclic define reference: {name}")
    seen.add(name)

    if name not in table:
        raise KeyError(name)
    expr = table[name].strip()

    if re.fullmatch(r"-?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|[0-9]+)", expr):
        return parse_c_int(expr)
    if re.fullmatch(r"-?[A-Za-z_][A-Za-z0-9_]*", expr):
        neg = expr.startswith("-")
        ref = expr[1:] if neg else expr
        value = resolve_numeric(ref, table, seen)
        return -value if neg else value
    raise ValueError(f"{name}: non-numeric or complex expression '{expr}'")


def check_group(group: str, tracked: list[str], upstream: dict[str, str], local: dict[str, str]) -> list[str]:
    errors: list[str] = []
    for name in tracked:
        if name not in local:
            errors.append(f"[{group}] missing local define: {name}")
            continue
        if name not in upstream:
            errors.append(f"[{group}] missing upstream define: {name}")
            continue
        try:
            up_val = resolve_numeric(name, upstream)
            loc_val = resolve_numeric(name, local)
        except Exception as exc:  # noqa: BLE001
            errors.append(f"[{group}] {name}: resolve error: {exc}")
            continue
        if up_val != loc_val:
            errors.append(f"[{group}] {name}: local={loc_val} upstream={up_val}")
    return errors


def main() -> int:
    root = Path(__file__).resolve().parent.parent

    upstream_errno = parse_defines(root / "third_party/bsd/freebsd-src/sys/sys/errno.h")
    upstream_fcntl = parse_defines(root / "third_party/bsd/freebsd-src/sys/sys/fcntl.h")
    upstream_wait = parse_defines(root / "third_party/bsd/freebsd-src/sys/sys/wait.h")
    upstream_signal = parse_defines(root / "third_party/bsd/freebsd-src/include/signal.h")
    upstream_stat = parse_defines(root / "third_party/bsd/freebsd-src/sys/sys/stat.h")
    upstream_mman = parse_defines(root / "third_party/bsd/freebsd-src/sys/sys/mman.h")

    local_errno = parse_defines(root / "userland/include/sys/errno.h")
    local_fcntl = parse_defines(root / "userland/include/sys/fcntl.h")
    local_wait = parse_defines(root / "userland/include/sys/wait.h")
    local_signal = parse_defines(root / "userland/include/sys/signal.h")
    local_stat = parse_defines(root / "userland/include/sys/stat.h")
    local_mman = parse_defines(root / "userland/include/sys/mman.h")

    errno_names = [
        "EPERM", "ENOENT", "ESRCH", "EINTR", "EIO", "ENXIO", "E2BIG", "ENOEXEC", "EBADF",
        "ECHILD", "EDEADLK", "ENOMEM", "EACCES", "EFAULT", "EBUSY", "EEXIST", "ENODEV",
        "ENOTDIR", "EISDIR", "EINVAL", "ENFILE", "EMFILE", "ENOSPC", "ESPIPE", "EROFS",
        "EPIPE", "ERANGE", "EAGAIN", "ENOSYS",
    ]
    fcntl_names = [
        "O_RDONLY", "O_WRONLY", "O_RDWR", "O_ACCMODE", "O_NONBLOCK", "O_APPEND", "O_SYNC",
        "O_NOFOLLOW", "O_CREAT", "O_TRUNC", "O_EXCL", "O_NOCTTY", "F_GETFD", "F_SETFD",
        "F_GETFL", "F_SETFL", "FD_CLOEXEC", "AT_FDCWD",
    ]
    wait_names = [
        "WNOHANG", "WUNTRACED", "WCONTINUED", "WNOWAIT", "WEXITED", "WTRAPPED",
    ]
    signal_names = [
        "SIG2STR_MAX",
    ]
    stat_names = [
        "S_IFMT", "S_IFDIR", "S_IFREG", "S_IRUSR", "S_IWUSR", "S_IXUSR",
    ]
    mman_names = [
        "PROT_NONE", "PROT_READ", "PROT_WRITE", "PROT_EXEC",
        "MAP_SHARED", "MAP_PRIVATE", "MAP_FIXED", "MAP_ANON", "MAP_ANONYMOUS",
        "MS_SYNC", "MS_ASYNC", "MS_INVALIDATE",
    ]

    errors: list[str] = []
    errors.extend(check_group("errno", errno_names, upstream_errno, local_errno))
    errors.extend(check_group("fcntl", fcntl_names, upstream_fcntl, local_fcntl))
    errors.extend(check_group("wait", wait_names, upstream_wait, local_wait))
    errors.extend(check_group("signal", signal_names, upstream_signal, local_signal))
    errors.extend(check_group("stat", stat_names, upstream_stat, local_stat))
    errors.extend(check_group("mman", mman_names, upstream_mman, local_mman))

    if errors:
        print("BSD ABI header check failed:")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("BSD ABI header check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
