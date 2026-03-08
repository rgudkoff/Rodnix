# План исполнения: ядро как основа полноценной ОС

Этот документ фиксирует приоритизированный план работ после достижения
минимального userland path (`run /bin/init` + POSIX smoke test).

## Цель

Довести RodNIX от рабочего ядра с минимальным ring3 до стабильной основы ОС:

- воспроизводимая загрузка и запуск;
- предсказуемая модель процессов/памяти;
- расширяемый и стабильный syscall ABI;
- тестируемость и наблюдаемость в CI.

## Срез текущего состояния (на сейчас)

- BIOS‑загрузка в QEMU подтверждена (`make run`).
- Рабочий путь userland: `run /bin/init`.
- В userland проверены базовые syscall-и:
  `getpid`, `open/read/close`, `write`, `exit`.
- Ввод в `-nographic` режиме доступен через serial fallback.

## Статус P0 (текущий прогресс)

- Сделано:
  - базовая валидация user pointers в `open/read/write/uname`;
  - стабилизирован вызов `run /bin/init` (повторный запуск без зависания shell);
  - добавлен deferred reaper queue для завершённых потоков;
  - reaper v1.5: освобождение thread descriptor + teardown task без активных потоков;
  - smoke `boot -> shell -> run /bin/init -> shell` подтверждён.
- В работе:
  - безопасный teardown kernel stack завершённого user thread
    (сейчас stack teardown отложен, чтобы избежать heap corruption).

## P0 (критический путь, 2-4 недели)

1. Process lifecycle (минимальный)
   - Корректное освобождение `task/thread/address_space` после `exit`.
   - Явная модель состояний процесса и точки очистки ресурсов.
   - Блокировки shell/user-thread без зависаний в ошибочных ветках.

2. Syscall namespace/ABI стабилизация
   - Развести legacy `SYS_*` и `POSIX_SYS_*` без коллизий.
   - Зафиксировать таблицу номеров и статус (`stable/experimental`).
   - Единая политика возврата ошибок (errno-like mapping).

3. User memory safety baseline
   - Минимальный `copy_from_user/copy_to_user`.
   - Валидация пользовательских указателей в syscall handlers.
   - Явные fault-path для bad user pointers.

4. CI smoke для boot + userland
   - Headless прогон QEMU в CI.
   - Проверка маркеров: boot ok, `rodnix>`, `POSIX smoke test done`.

Критерий готовности P0:
- 100% локально и в CI проходит smoke:
  boot -> shell -> `run /bin/init` -> возврат в shell без panic/hang.

## P1 (базовая ОС-функциональность, 4-8 недель)

1. POSIX Level-1 (ядро ядра)
   - `openat`, `lseek`, `stat/fstat`, `chdir/getcwd`.
   - `dup2`, `pipe`, `fcntl(F_GETFD/F_SETFD)` и `FD_CLOEXEC`.
   - `clock_gettime` (`REALTIME`, `MONOTONIC`).

2. Process model v1
   - `fork` (без COW на первом шаге допустимо через full copy).
   - `execve` с корректным argv/envp и обновлением address space.
   - `waitpid` + базовый `SIGCHLD`.

3. VM/user address space v1
   - User VM map как явная структура (`vm_map_entry`-подобная модель).
   - VM object слой для anonymous/file-backed диапазонов.
   - Page fault path: map lookup -> object resolve -> pmap enter.
   - Базовый COW для `fork`.
   - Каркас `mmap/munmap/brk` для libc.

4. VFS semantics v1
   - Базовые права доступа и корректные file flags.
   - Минимально достаточная path semantics без регрессий shell/userland.

Критерий готовности P1:
- Запускается простой userland workflow:
  `execve` + чтение/запись файлов + дочерний процесс + `waitpid`.
- Планировщик поддерживает QoS bucket policy v1 (без EDF), с наблюдаемыми метриками.

## P2 (подготовка к промышленной эксплуатации, 8+ недель)

1. Надёжность/наблюдаемость
   - Структурированные логи (уровни/категории).
   - Трассировка irq/sched/syscall/fault.
   - Crash dump: регистры + backtrace + контекст задачи.

2. Безопасность
   - Последовательные проверки UID/GID в syscall/VFS/IPC.
   - Жёсткие границы kernel/user и формализация trusted/untrusted.
   - Каркас MAC hooks.

3. IPC/IDL production-path
   - Namespace/rights model для портов.
   - Variable-size payload + права/передача портов с валидацией.
   - Интеграция автогенерации IDL в build/test pipeline.

4. Сеть и сервисы
   - UDP/TCP progression после стабилизации process + VM + ABI.
   - Базовые сетевые smoke/regression тесты.

- Планировщик v2 (clutch-like подход)
  - Иерархия `bucket -> thread_group -> thread`.
  - EDF-like выбор bucket по latency budget/deadline.
  - `warp window` для интерактивных всплесков.
  - `starvation avoidance window` для bounded fairness.

5. VM v2 (слоистые принципы)
  - Явное разделение ответственности `vm_map -> vm_object -> pmap`.
  - Pager/backing-store интерфейс для file/anonymous объектов.
  - Модель page states (active/inactive/wired/pager-backed) и наблюдаемость.
  - Memorystatus policy: system health loop, thresholds, jetsam-like bands.

Критерий готовности P2:
- Повторяемые интеграционные тесты по boot/process/fs/ipc/network без ручных шагов.
- Стресс-тест планировщика показывает bounded starvation между bucket/group.
- VM стресс-тест (fault/COW/map/unmap) проходит без утечек/коррупции.

## Ближайший backlog (следующие 2 спринта)

Sprint A:
- lifecycle cleanup для `run`/`exit`;
- user pointer validation в `read/write/open/uname`;
- CI smoke для `run /bin/init`.

Sprint B:
- `openat/lseek/stat/getcwd/chdir`;
- `dup2/pipe/fcntl(F_GETFD/F_SETFD)`;
- документация ABI + фиксированная таблица syscall номеров.

## Связанные документы

- `docs/ru/p0_focus_plan.md`
- `docs/ru/industrial_gap.md`
- `docs/ru/unix_process_model.md`
- `docs/archive/posix-plan.md` (архив, только как история решений)
