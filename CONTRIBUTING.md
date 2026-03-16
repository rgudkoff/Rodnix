# Contributing Guidelines

## Scope

This document defines how changes should be prepared and submitted so the
repository history stays readable and reviewable.

## Before You Start

1. Check open issues and existing plans in `docs/`.
2. Keep each change focused on one problem.
3. For large or cross-subsystem work, open an issue first and agree on scope.

## Branching

1. Branch from `main`.
2. Use descriptive branch names:
   - `fix/<area>-<issue>`
   - `feat/<area>-<capability>`
   - `refactor/<area>-<goal>`
3. Avoid long-lived branches that mix unrelated work.

## Code Standards

1. Prefer clear, explicit code over compact clever patterns.
2. Keep subsystem boundaries intact (`arch`, `core`, `fabric`, `fs`, `posix`, `userland`).
3. Add comments only where intent is not obvious from code.
4. Do not introduce dead code, placeholder paths, or commented-out logic.

## Commit Style

One commit must represent one logical change.

Commit message format:

```text
<type>(<scope>): <short summary>

Why:
- problem being solved

What:
- key implementation points

Validation:
- commands/tests run
```

Recommended `type` values: `fix`, `feat`, `refactor`, `docs`, `build`, `test`, `ci`.

Rules:

1. No `WIP`/`tmp`/`misc` commit subjects.
2. Squash fixup commits before opening PR.
3. Do not mix refactor and behavior change in the same commit unless required.
4. Include validation details for kernel/userland behavior changes.

## Pull Request Style

A PR should be small enough for one focused review session.

PR description must include:

1. Problem statement.
2. Design decision and tradeoffs.
3. Exact user-visible or kernel-visible behavior change.
4. Validation evidence (build/test/smoke output summary).
5. Risks and rollback strategy (if applicable).

## Validation Baseline

For most changes:

1. `make`
2. Relevant CI smoke script(s) from `scripts/ci/`
3. Any targeted checks for modified subsystem

If you skip tests, explicitly state why in the PR.

## Documentation Quality

1. Keep docs concrete and operational.
2. Prefer local repository paths over machine-specific absolute paths.
3. Remove stale notes and duplicate sections during related edits.
4. Update docs in the same PR when behavior or workflow changes.

## Licensing

All contributions must follow project licensing and third-party notice rules.
For imported external code, update:

1. `third_party/bsd/SOURCES.md`
2. `THIRD_PARTY_NOTICES.md`
