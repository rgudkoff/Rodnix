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

- Upstream: `https://github.com/freebsd/freebsd-src`
- Upstream branch/tag: `main`
- Upstream commit: `not pinned (local file snapshot from /Users/romangudkov/dev/bsd/sys/sys)`
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
