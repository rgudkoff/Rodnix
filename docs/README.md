# RodNIX Documentation

The `docs/` tree is intentionally split into three zones:

- `docs/ru/` — active Russian-language documentation;
- `docs/en/` — English-language documentation and navigation mirrors;
- `docs/archive/` and `docs/ru/archive/` — historical and archived material.

This directory should be treated as a documentation tree with explicit entry
points, not as a flat dump of notes.

Current release baseline: `0.1.22`.

## Start Here

- `docs/ru/README.md` — active Russian documentation map;
- `docs/en/README.md` — English documentation index;
- `docs/ru/overview.md` — project scope and principles;
- `docs/ru/architecture.md` — current architecture map;
- `docs/ru/build_run.md` — practical build and run guide;
- `docs/ru/debugging.md` — diagnostics and debug workflow.
- `README.md` — top-level release and runtime entry notes.

## Active Documentation Sets

- `docs/ru/` — source-of-truth active engineering documents;
- `docs/en/` — English navigation layer and mirrors for repository entry;
- `docs/archive/`, `docs/ru/archive/` — retired plans, historical specs,
  and documents kept only for reference.

## Rules

- new decisions and process contracts belong only in the active documentation
  zones;
- archive material must not be treated as the current specification;
- when system behavior changes, the related active documentation must be updated
  in the same changeset;
- entry-point and operational documents should favor precision, reproducibility,
  and direct wording.
