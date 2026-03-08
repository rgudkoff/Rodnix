# Документация RodNIX (RU)

Эта документация — рабочая основа промышленного качества RodNIX.
Она описывает архитектуру, принципы, интерфейсы подсистем и
формальные критерии готовности. Мы используем её как "контракт",
по которому ведётся развитие системы.

## Назначение

- Зафиксировать архитектурные решения и границы подсистем.
- Обеспечить детерминизм, наблюдаемость и управляемость отказов.
- Поддержать совместимость интерфейсов и устойчивость к изменениям.

## Текущий статус

RodNIX находится в активной разработке промышленного ядра.
Фактическое состояние подсистем может меняться, поэтому каждый раздел
содержит блоки "Что уже есть" и "Что планируется".

Актуально на 8 марта 2026:

- Boot path стабилен: `boot -> userspace /bin/init -> /bin/sh`.
- Внешние IRQ работают в IOAPIC-first режиме; PIC отключается при успешном IOAPIC.
- Включение прерываний централизовано в фазе `INIT-10` (без `sti` внутри драйверов).
- Userspace shell работает интерактивно, поддерживает запуск внешних программ
  (`<program> [args...]` и абсолютные пути).
- В VFS присутствуют виртуальные консольные узлы `/dev/console`,
  `/dev/stdin`, `/dev/stdout`, `/dev/stderr`.
- `devfs` как отдельная ФС и `/dev/fd/{0,1,2}` (с alias-ссылками
  `stdin/stdout/stderr`) пока не внедрены, это следующий этап.
- Для userland ABI (`errno/fcntl/wait`) введена автоматическая проверка
  соответствия FreeBSD-канону (`check-bsd-abi` в `userland/Makefile`).

## Фокус релиза v0.2

- Стабильный переход загрузки → long mode → `kmain`.
- Higher-half mapping ядра + ранний physmap.
- VFS + импорт initrd (формат `RDNX`).
- Минимальный набор syscalls и per-task fd таблица.
- IPC + минимальный IDL runtime.
- Актуальные и непротиворечивые документы.

## Как пользоваться

- Начинайте с `overview.md` и `architecture.md`.
- Для работы над конкретной подсистемой читайте ее профильный файл.
- Для сборки и запуска используйте `build_run.md`.
- Дорожная карта и план работ — в `development_plan.md`.
- Исполнимый приоритетный план (P0/P1/P2) — в `execution_plan_os_foundation.md`.
- Операционный фокус по P0 (что делаем прямо сейчас) — в `p0_focus_plan.md`.

## Содержание

- `overview.md` — цели, принципы и границы проекта.
- `architecture.md` — общая архитектура и структура дерева.
- `kernel_layout_refactor.md` — целевая реорганизация структуры файлов ядра.
- `xnu_crosswalk.md` — соответствие подсистем RodNIX и структуры XNU.
- `bsd_import_plan.md` — поэтапный план заимствования BSD-компонентов.
- `bsd_posix_userland_plan.md` — план переноса POSIX/userland на BSD-базу.
- `include/bsd/sys/*` — импортированные BSD-примитивы (`queue.h`, `tree.h`),
  используемые в ядре как рабочий compatibility-layer.
- `boot.md` — путь загрузки, переход в long mode и boot-логирование.
- `boot_args.md` — поддерживаемые boot-аргументы и режимы bootstrap.
- `memory.md` — память: PMM, VMM, план и инварианты.
- `memory_model.md` — формальная модель памяти и OOM.
- `memorystatus.md` — memory pressure kill/recovery policy (jetsam/freezer/notifications).
- `interrupts.md` — прерывания, таймеры, IRQ-потоки.
- `fabric.md` — Fabric: шины, устройства, драйверы, сервисы.
- `input.md` — путь клавиатуры и InputCore.
- `scheduler.md` — планировщик и политика приоритетов.
  Включает roadmap к clutch-like модели: `bucket -> thread_group -> thread`,
  QoS-aware вытеснение и anti-starvation окна.
- `ipc.md` — IPC и передача портов.
- `idl.md` — IDL и генерация IPC‑стабов.
- `userspace.md` — userland и bootstrap‑сервер.
- `network.md` — сетевой стек и порядок внедрения.
- `syscalls.md` — системные вызовы и вход в ядро.
- `security.md` — учёт и права (UID/GID, MAC).
- `vfs.md` — VFS: vnode/inode, кэш имён, монтирование, initrd/ramfs.
- `build_run.md` — сборка, запуск, отладка.
- `conventions.md` — инженерные соглашения и стиль.
- `development_plan.md` — приоритеты и этапы.
- `execution_plan_os_foundation.md` — практический план исполнения с критериями готовности.
- `p0_focus_plan.md` — приоритизированный фокус стабилизации P0.
- `debugging.md` — диагностика и инструменты.
- `industrial_readiness.md` — критерии промышленной готовности.
- `industrial_gap.md` — gap‑анализ по критериям готовности.
- `failure_model.md` — детерминизм, ошибки и fail‑fast.

## Связанные документы (EN)

- `README.md`
- `ARCHITECTURE.md`
- `64BIT_MIGRATION.md`
- `docs/BUILD.md`
- `docs/PLAN.md`
- `ROADMAP.md`
