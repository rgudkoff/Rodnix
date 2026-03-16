# Contributing Guidelines

## Purpose

This document defines the minimum rules for preparing changes so repository
history, code review, and operational documentation stay manageable.

## Before You Start

1. Check the active documents under `docs/ru/`.
2. Keep each change focused on one problem or one engineering objective.
3. For large or cross-subsystem work, align the scope before implementation.

## Branching

Recommended approach:

1. start from the current team baseline branch;
2. use descriptive branch names;
3. do not mix unrelated work in the same branch.

Examples:

- `fix/<area>-<issue>`
- `feat/<area>-<capability>`
- `refactor/<area>-<goal>`
- `docs/<area>-<topic>`

## Change Requirements

- preserve subsystem boundaries (`arch`, `core`, `fabric`, `fs`, `posix`,
  `userland`);
- do not add dead code, temporary workaround paths without clear purpose, or
  commented-out logic;
- source code comments must be short, technical, and written in English;
- when behavior changes, update the relevant documentation in the same PR or
  changeset;
- for imported external code, update the related notice and source metadata.

## Commit Rules

One commit should represent one logical change.

Recommended format:

```text
<type>(<scope>): <short summary>

Why:
- problem being solved

What:
- key implementation points

Validation:
- commands/tests run
```

Recommended `type` values:

- `fix`
- `feat`
- `refactor`
- `docs`
- `build`
- `test`
- `ci`

Mandatory rules:

1. do not use `WIP`, `tmp`, `misc`, or similar subjects;
2. do not mix refactoring and behavior change unless it is necessary;
3. squash noisy fixup commits before submission;
4. if tests were not run, state that explicitly.

## Pull Request Expectations

A PR should be small enough for one focused review session.

The PR description should include:

1. the problem statement;
2. the chosen design and trade-offs;
3. the observable behavior change;
4. validation evidence;
5. risks and rollback strategy, when applicable.

## Minimum Validation

For most changes:

1. `make`
2. `make -C userland`
3. relevant smoke or CI scripts from `scripts/ci/`
4. targeted checks for the modified subsystem

If a change affects kernel or userland ABI, also verify the related
documentation and utility compatibility.

## Documentation Quality

- keep documentation concrete and operational;
- use repository paths instead of local absolute paths;
- remove stale duplicates and temporary notes during related edits;
- do not treat archive plans as active specification.

## Licensing and Third-Party Material

For imported external code and related changes, update:

1. `third_party/bsd/SOURCES.md`
2. `THIRD_PARTY_NOTICES.md`

All changes must remain consistent with the project licensing policy.
