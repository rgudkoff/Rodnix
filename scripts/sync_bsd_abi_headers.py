#!/usr/bin/env python3
"""
Sync selected userland ABI headers from FreeBSD vendor headers.

This keeps numeric POSIX ABI constants aligned with:
  third_party/bsd/{include,sys/sys}/*
"""

from __future__ import annotations

from pathlib import Path
import re


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


def fmt_hex(v: int) -> str:
    return f"0x{v:04x}" if v >= 0 else f"-0x{(-v):x}"


def fmt_oct(v: int) -> str:
    if v < 0:
        return f"-0{(-v):o}"
    return f"0{v:o}"


def write_if_changed(path: Path, content: str) -> None:
    old = path.read_text(encoding="utf-8") if path.exists() else None
    if old == content:
        return
    path.write_text(content, encoding="utf-8")


def get_vals(names: list[str], table: dict[str, str]) -> dict[str, int]:
    out: dict[str, int] = {}
    for name in names:
        out[name] = resolve_numeric(name, table)
    return out


def render_errno(vals: dict[str, int]) -> str:
    names = [
        "EPERM", "ENOENT", "ESRCH", "EINTR", "EIO", "ENXIO", "E2BIG", "ENOEXEC", "EBADF",
        "ECHILD", "EDEADLK", "ENOMEM", "EACCES", "EFAULT", "EBUSY", "EEXIST", "ENODEV",
        "ENOTDIR", "EISDIR", "EINVAL", "ENFILE", "EMFILE", "ENOSPC", "ESPIPE", "EROFS",
        "EPIPE", "ERANGE", "EAGAIN", "ENOSYS",
    ]
    lines = [
        "#ifndef _RODNIX_USERLAND_SYS_ERRNO_H",
        "#define _RODNIX_USERLAND_SYS_ERRNO_H",
        "",
        "/*",
        " * Generated from FreeBSD headers by scripts/sync_bsd_abi_headers.py.",
        " * Source: third_party/bsd/sys/sys/errno.h",
        " */",
    ]
    for n in names:
        lines.append(f"#define {n:<7} {vals[n]}")
    lines.extend(["", "#endif /* _RODNIX_USERLAND_SYS_ERRNO_H */", ""])
    return "\n".join(lines)


def render_fcntl(vals: dict[str, int]) -> str:
    lines = [
        "#ifndef _RODNIX_USERLAND_SYS_FCNTL_H",
        "#define _RODNIX_USERLAND_SYS_FCNTL_H",
        "",
        "/*",
        " * Generated from FreeBSD headers by scripts/sync_bsd_abi_headers.py.",
        " * Source: third_party/bsd/sys/sys/fcntl.h",
        " */",
        f"#define O_RDONLY  {fmt_hex(vals['O_RDONLY'])}",
        f"#define O_WRONLY  {fmt_hex(vals['O_WRONLY'])}",
        f"#define O_RDWR    {fmt_hex(vals['O_RDWR'])}",
        f"#define O_ACCMODE {fmt_hex(vals['O_ACCMODE'])}",
        "",
        f"#define O_NONBLOCK {fmt_hex(vals['O_NONBLOCK'])}",
        f"#define O_APPEND   {fmt_hex(vals['O_APPEND'])}",
        f"#define O_SYNC     {fmt_hex(vals['O_SYNC'])}",
        f"#define O_NOFOLLOW {fmt_hex(vals['O_NOFOLLOW'])}",
        f"#define O_CREAT    {fmt_hex(vals['O_CREAT'])}",
        f"#define O_TRUNC    {fmt_hex(vals['O_TRUNC'])}",
        f"#define O_EXCL     {fmt_hex(vals['O_EXCL'])}",
        f"#define O_NOCTTY   {fmt_hex(vals['O_NOCTTY'])}",
        "",
        f"#define F_GETFD {vals['F_GETFD']}",
        f"#define F_SETFD {vals['F_SETFD']}",
        f"#define F_GETFL {vals['F_GETFL']}",
        f"#define F_SETFL {vals['F_SETFL']}",
        "",
        f"#define FD_CLOEXEC {vals['FD_CLOEXEC']}",
        "",
        f"#define AT_FDCWD {vals['AT_FDCWD']}",
        "",
        "#endif /* _RODNIX_USERLAND_SYS_FCNTL_H */",
        "",
    ]
    return "\n".join(lines)


def render_wait(vals: dict[str, int]) -> str:
    lines = [
        "#ifndef _RODNIX_USERLAND_SYS_WAIT_H",
        "#define _RODNIX_USERLAND_SYS_WAIT_H",
        "",
        "#include <sys/types.h>",
        "",
        "#define _W_INT(i) ((i))",
        "#define _WSTATUS(x) (_W_INT(x) & 0177)",
        "#define _WSTOPPED 0177",
        "",
        "#define WIFSTOPPED(x) (_WSTATUS(x) == _WSTOPPED)",
        "#define WSTOPSIG(x) (_W_INT(x) >> 8)",
        "#define WIFSIGNALED(x) (_WSTATUS(x) != _WSTOPPED && _WSTATUS(x) != 0 && (x) != 0x13)",
        "#define WTERMSIG(x) (_WSTATUS(x))",
        "#define WIFEXITED(x) (_WSTATUS(x) == 0)",
        "#define WEXITSTATUS(x) (_W_INT(x) >> 8)",
        "#define WIFCONTINUED(x) ((x) == 0x13)",
        "",
        f"#define WNOHANG   {vals['WNOHANG']}",
        f"#define WUNTRACED {vals['WUNTRACED']}",
        "#define WSTOPPED  WUNTRACED",
        f"#define WCONTINUED {vals['WCONTINUED']}",
        f"#define WNOWAIT   {vals['WNOWAIT']}",
        f"#define WEXITED   {vals['WEXITED']}",
        f"#define WTRAPPED  {vals['WTRAPPED']}",
        "",
        "#endif /* _RODNIX_USERLAND_SYS_WAIT_H */",
        "",
    ]
    return "\n".join(lines)


def render_signal(vals: dict[str, int]) -> str:
    lines = [
        "#ifndef _RODNIX_USERLAND_SYS_SIGNAL_H",
        "#define _RODNIX_USERLAND_SYS_SIGNAL_H",
        "",
        "/*",
        " * Generated from FreeBSD headers by scripts/sync_bsd_abi_headers.py.",
        " * Source: third_party/bsd/include/signal.h",
        " */",
        f"#define SIG2STR_MAX {vals['SIG2STR_MAX']}",
        "",
        "#endif /* _RODNIX_USERLAND_SYS_SIGNAL_H */",
        "",
    ]
    return "\n".join(lines)


def render_stat(vals: dict[str, int]) -> str:
    lines = [
        "#ifndef _RODNIX_USERLAND_SYS_STAT_H",
        "#define _RODNIX_USERLAND_SYS_STAT_H",
        "",
        "#include <sys/types.h>",
        "",
        "struct stat {",
        "    mode_t st_mode;",
        "    off_t st_size;",
        "};",
        "",
        f"#define S_IFMT   {fmt_oct(vals['S_IFMT'])}",
        f"#define S_IFREG  {fmt_oct(vals['S_IFREG'])}",
        f"#define S_IFDIR  {fmt_oct(vals['S_IFDIR'])}",
        f"#define S_IRUSR  {fmt_oct(vals['S_IRUSR'])}",
        f"#define S_IWUSR  {fmt_oct(vals['S_IWUSR'])}",
        f"#define S_IXUSR  {fmt_oct(vals['S_IXUSR'])}",
        "",
        "#endif /* _RODNIX_USERLAND_SYS_STAT_H */",
        "",
    ]
    return "\n".join(lines)


def render_mman(vals: dict[str, int]) -> str:
    lines = [
        "#ifndef _RODNIX_USERLAND_SYS_MMAN_H",
        "#define _RODNIX_USERLAND_SYS_MMAN_H",
        "",
        "/*",
        " * Generated from FreeBSD headers by scripts/sync_bsd_abi_headers.py.",
        " * Source: third_party/bsd/sys/sys/mman.h",
        " */",
        f"#define PROT_NONE   {fmt_hex(vals['PROT_NONE'])}",
        f"#define PROT_READ   {fmt_hex(vals['PROT_READ'])}",
        f"#define PROT_WRITE  {fmt_hex(vals['PROT_WRITE'])}",
        f"#define PROT_EXEC   {fmt_hex(vals['PROT_EXEC'])}",
        "",
        f"#define MAP_SHARED   {fmt_hex(vals['MAP_SHARED'])}",
        f"#define MAP_PRIVATE  {fmt_hex(vals['MAP_PRIVATE'])}",
        f"#define MAP_FIXED    {fmt_hex(vals['MAP_FIXED'])}",
        f"#define MAP_ANON     {fmt_hex(vals['MAP_ANON'])}",
        "#define MAP_ANONYMOUS MAP_ANON",
        "",
        f"#define MS_SYNC       {fmt_hex(vals['MS_SYNC'])}",
        f"#define MS_ASYNC      {fmt_hex(vals['MS_ASYNC'])}",
        f"#define MS_INVALIDATE {fmt_hex(vals['MS_INVALIDATE'])}",
        "",
        "#endif /* _RODNIX_USERLAND_SYS_MMAN_H */",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    upstream_errno = parse_defines(root / "third_party/bsd/sys/sys/errno.h")
    upstream_fcntl = parse_defines(root / "third_party/bsd/sys/sys/fcntl.h")
    upstream_wait = parse_defines(root / "third_party/bsd/sys/sys/wait.h")
    upstream_signal = parse_defines(root / "third_party/bsd/include/signal.h")
    upstream_stat = parse_defines(root / "third_party/bsd/sys/sys/stat.h")
    upstream_mman = parse_defines(root / "third_party/bsd/sys/sys/mman.h")

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
    wait_names = ["WNOHANG", "WUNTRACED", "WCONTINUED", "WNOWAIT", "WEXITED", "WTRAPPED"]
    signal_names = ["SIG2STR_MAX"]
    stat_names = ["S_IFMT", "S_IFDIR", "S_IFREG", "S_IRUSR", "S_IWUSR", "S_IXUSR"]
    mman_names = [
        "PROT_NONE", "PROT_READ", "PROT_WRITE", "PROT_EXEC",
        "MAP_SHARED", "MAP_PRIVATE", "MAP_FIXED", "MAP_ANON",
        "MS_SYNC", "MS_ASYNC", "MS_INVALIDATE",
    ]

    errno_vals = get_vals(errno_names, upstream_errno)
    fcntl_vals = get_vals(fcntl_names, upstream_fcntl)
    wait_vals = get_vals(wait_names, upstream_wait)
    signal_vals = get_vals(signal_names, upstream_signal)
    stat_vals = get_vals(stat_names, upstream_stat)
    mman_vals = get_vals(mman_names, upstream_mman)

    write_if_changed(root / "userland/include/sys/errno.h", render_errno(errno_vals))
    write_if_changed(root / "userland/include/sys/fcntl.h", render_fcntl(fcntl_vals))
    write_if_changed(root / "userland/include/sys/wait.h", render_wait(wait_vals))
    write_if_changed(root / "userland/include/sys/signal.h", render_signal(signal_vals))
    write_if_changed(root / "userland/include/sys/stat.h", render_stat(stat_vals))
    write_if_changed(root / "userland/include/sys/mman.h", render_mman(mman_vals))

    print("Synced userland ABI headers from FreeBSD vendor snapshot")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
