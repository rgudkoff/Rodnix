# Документация RodNIX (RU)

Актуальная документация разделена на компактный `core`-набор.
Устаревшие, дублирующие и исторические материалы вынесены в архив.

## Старт

- `overview.md` — границы проекта и принципы.
- `architecture.md` — текущая архитектура и карта подсистем.
- `build_run.md` — практическая сборка и запуск.
- `execution_plan_os_foundation.md` — основной исполнимый план.
- `p0_focus_plan.md` — текущий фокус стабилизации.

## Core-документы

- `adr_darwin_layering.md` — целевая слоистая модель
  (`kernel mechanisms -> unix layer -> posix ABI`).
- `unix_layer_contract.md` — контракт и границы `kernel/unix`.
- `unix_process_model.md` — модель `spawn/exec/wait/exit`.
- `unix_process_contract_tests.md` — матрица инвариантов и тестов (CT-xxx).
- `contract_governance.md` — правила CI-gate и эволюции CT-контрактов.
- `bsd_import_plan.md` — стратегия интеграции BSD-кода.
- `bsd_posix_userland_plan.md` — план POSIX userland поверх BSD ABI.
- `boot.md` — путь загрузки.
- `memory.md` — модель памяти и текущие ограничения.
- `scheduler.md` — модель планировщика.
- `syscalls.md` — syscall ABI и правила.
- `vfs.md` — VFS и каталог/FD семантика.
- `userspace.md` — userland bootstrap и runtime.
- `debugging.md` — отладка и диагностика.
- `conventions.md` — инженерные правила.
- `industrial_readiness.md` — критерии готовности к выпуску.
- `industrial_gap.md` — gap-анализ к критериям готовности.
- `failure_model.md` — fail-fast и модель ошибок.

## Архив

- `docs/ru/archive/` — legacy-документы (не являются текущей спецификацией).
- `docs/archive/` — архив старых EN high-level планов.

## Связанные документы (EN)

- `README.md`
- `ARCHITECTURE.md`
- `64BIT_MIGRATION.md`
- `ROADMAP.md`
- `docs/README.md`
