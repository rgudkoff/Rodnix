# Third-Party Notices

This project may include third-party code imported from external open-source
projects. For each imported component, we keep:

- original source location;
- upstream commit/revision;
- applicable license;
- list of copied files and local modifications.

BSD-derived imports are tracked in:

- `third_party/bsd/SOURCES.md`

Current imported third-party files include:

- `include/bsd/sys/queue.h` (from FreeBSD `sys/sys/queue.h`)
- `include/bsd/sys/tree.h` (from FreeBSD `sys/sys/tree.h`)

These files retain original upstream copyright/license headers.

Rules for adding third-party code:

1. Keep original copyright and license headers in imported files.
2. Record upstream URL and commit hash in `third_party/bsd/SOURCES.md`.
3. Record every local modification relative to upstream.
4. Do not mix files with incompatible license obligations in one import entry.

This notice file is intentionally concise; per-import details live next to the
imported code metadata in `third_party/`.
