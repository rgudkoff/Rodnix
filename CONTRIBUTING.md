# Contributing Guidelines

Thank you for your interest in contributing!

Before submitting any contribution, please read these rules carefully.

---

## 1. Contributor License Agreement (CLA)

All contributors must sign the CLA before their Pull Requests can be merged.

This ensures:
- legal clarity,
- the ability to re-license or dual-license in the future,
- long-term sustainability of the project.

See: CLA_NOTICE.md

---

## 2. How to Contribute

1. Fork the repository.
2. Create a feature branch (`feature/my-change`).
3. Make your changes.
4. Ensure the build passes: `make` + all CI tests.
5. Submit a Pull Request against the `main` branch.
6. Describe your change clearly and reference any related Issues.

One PR = one logical change. Large changes must be discussed in Issues before PR creation.

---

## 3. Coding Standards

### C Code
- Follow the kernel's formatting rules.
- No warnings in compilation (`-Wall -Wextra`).
- No forbidden functions, avoid undefined behavior.

### Assembly
- NASM syntax (elf32).
- Keep comments clear and concise.

---

## 4. Commit Messages

Use meaningful messages:
