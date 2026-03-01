# BSD Vendor Workspace

`third_party/bsd` is the staging area for BSD-origin imports used in Rodnix.

## Layout

- `third_party/bsd/SOURCES.md`
  - Canonical import registry: upstream URL, commit, paths, local modifications.
- `third_party/bsd/IMPORT_TEMPLATE.md`
  - Template for adding new import entries.
- `third_party/bsd/freebsd-src/`
  - Verbatim upstream snapshot files used as local reference/vendor baseline.

Current snapshot includes:

- `third_party/bsd/freebsd-src/bin/sh/*`
- `third_party/bsd/freebsd-src/include/*` (selected POSIX headers)
- `third_party/bsd/freebsd-src/sys/sys/*` (selected kernel/user ABI headers)

## Usage Rules

- Keep imported files verbatim whenever possible.
- Any local patch to imported code must be documented in `SOURCES.md`.
- Do not include BSD files directly into Rodnix build by default until compatibility shims are ready.
- Prefer incremental adoption: constants/types first, then APIs, then userland binaries.

## Next Integration Steps

- Build `userland` compatibility headers from vetted BSD constants/types.
- Introduce minimal libc/syscall shim to run external userland programs.
- Keep `third_party/bsd/freebsd-src` as immutable reference baseline.
