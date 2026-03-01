# BSD Import Template

Copy this block into `third_party/bsd/SOURCES.md` for every new import.

```md
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
  - `<none>` or list each local patch
- Notes:
  - optional
```

Checklist before merge:

- headers with original license text preserved in imported files;
- entry added to `third_party/bsd/SOURCES.md`;
- all local changes documented in `Modifications`;
- build and smoke tests passed.
