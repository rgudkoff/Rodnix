# BSD Imports Registry

This file is the canonical registry for BSD-origin code imported into Rodnix.

## Entry Format

Use one section per import:

### `<short-id>`

- Upstream: `<repository URL>`
- Upstream branch/tag: `<branch or tag>`
- Upstream commit: `<full commit hash>`
- Imported on: `<YYYY-MM-DD>`
- Imported by: `<name/handle>`
- License: `<BSD-2-Clause | BSD-3-Clause | BSD-4-Clause | ISC | ...>`
- Upstream path(s):
  - `<path/in/upstream>`
- Local path(s):
  - `<path/in/rodnix>`
- Modifications:
  - `<none>` or short bullet list of changes
- Notes:
  - optional compatibility notes

## Current Imports

### `freebsd-queue-tree-2026-03-01`

- Upstream: official FreeBSD source tree snapshot
- Upstream branch/tag: `main`
- Upstream commit: `not pinned (local file snapshot from local BSD mirror)`
- Imported on: `2026-03-01`
- Imported by: `codex`
- License: `BSD-2-Clause/BSD-3-Clause (as declared in file headers)`
- Upstream path(s):
  - `sys/sys/queue.h`
  - `sys/sys/tree.h`
- Local path(s):
  - `include/bsd/sys/queue.h`
  - `include/bsd/sys/tree.h`
- Modifications:
  - `none (verbatim copy)`
- Notes:
  - Imported from local FreeBSD snapshot directory provided by user.
  - `include/sys/cdefs.h` added as Rodnix compatibility shim for FreeBSD header dependencies.

### `freebsd-posix-userland-base-2026-03-01`

- Upstream: official FreeBSD source tree snapshot
- Upstream branch/tag: `main`
- Upstream commit: `5ddfd1db271cc675997a942da599c342ccb53afa`
- Imported on: `2026-03-01`
- Imported by: `codex`
- License: `BSD-2-Clause/BSD-3-Clause (per-file SPDX in imported files)`
- Upstream path(s):
  - `bin/sh/*`
  - `include/{unistd.h,stdio.h,stdlib.h,string.h,signal.h,limits.h,time.h,termios.h,dirent.h,pwd.h,grp.h}`
  - `sys/sys/{cdefs.h,queue.h,tree.h}` (from local user-provided snapshot)
- Local path(s):
  - `third_party/bsd/freebsd-src/bin/sh/*`
  - `third_party/bsd/freebsd-src/include/*`
  - `third_party/bsd/freebsd-src/sys/sys/*`
- Modifications:
  - `none (verbatim copy)`
- Notes:
  - This is a vendor/reference baseline for gradual POSIX/userland integration.
  - Not wired into Rodnix build yet.
