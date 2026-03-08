# Unix Process Contract Tests (v1)

Дата: 2026-03-08  
Статус: active

Источник истины:
- `docs/ru/unix_process_model.md`

Цель: связать каждый ключевой инвариант модели процесса с проверяемым тестом,
чтобы ловить semantic drift, а не только compile/runtime crashes.

## 1. Нотация

- `AUTO` — выполняется автоматически в CI.
- `MANUAL` — воспроизводится вручную в интерактивной сессии.
- `PLANNED` — тест спроектирован, но пока не автоматизирован.

Классы контрактов:

- `CORE` — обязательные для загрузочного CI.
- `LIFECYCLE` — процессные переходы состояний и reap/wait.
- `FD` — дескрипторная семантика.
- `EXEC` — `spawn/exec` контракты.
- `PLANNED` — зарезервированные, не гейтящие CI.

## 2. Матрица инвариантов

| ID | Класс | Инвариант | Тест | Статус |
|---|---|---|---|---|
| CT-001 | EXEC/CORE | `spawn` создаёт новый PID | contract mode в `userland/init/init.c` | AUTO |
| CT-002 | EXEC | `spawn` не заменяет текущий процесс | после `spawn` родитель продолжает выполнять следующий код | MANUAL |
| CT-003 | EXEC | `exec` сохраняет PID | contract mode в `userland/init/init.c` + `contract_exec_probe/after` | PLANNED |
| CT-004 | LIFECYCLE | `waitpid` только для дочернего процесса | ожидание не-дочернего `pid` возвращает `RDNX_E_DENIED` | PLANNED |
| CT-005 | LIFECYCLE | `exit` переводит в lifecycle ожидания (`ZOMBIE`/collect) | contract mode в `userland/init/init.c` | AUTO |
| CT-006 | LIFECYCLE | `waitpid` собирает статус и завершает lifecycle | contract mode: второй `waitpid` по тому же PID -> ошибка | AUTO |
| CT-007 | FD/CORE | `close(fd)` инвалидирует fd | contract mode в `userland/init/init.c` | AUTO |
| CT-008 | FD/CORE | `read/write` только для валидного fd | contract mode в `userland/init/init.c` | AUTO |
| CT-009 | CORE | pathname `readdir` возвращает корректные записи | `readdir("/")` возвращает ненулевой набор `dirent` | AUTO |
| CT-010 | CORE | user-pointer валидация не допускает kernel/non-canonical адреса | некорректный указатель -> `RDNX_E_INVALID`, без panic | PLANNED |
| CT-011 | EXEC | `exec` сбрасывает image-specific state | `contract_exec_after` возвращает флаг reset в wait status | PLANNED |
| CT-012 | CORE | polling с `SYS_NOP` даёт cooperative progress | child/reaper получают CPU, parent-loop не вызывает starvation | PLANNED |
| CT-013 | CORE | context switch переключает address space процесса | contract mode: parent stack canary survives child run/wait | AUTO |

## 3. Формат CI-маркеров

Единый формат в `boot.log`:

`[CT] <CT-ID> <PASS|FAIL> <message>`

Примеры:

- `[CT] CT-001 PASS spawn creates child pid`
- `[CT] CT-007 PASS close invalidates fd`
- `[CT] CT-005 FAIL waitpid failed or bad status`

Итоговый маркер:

- `[CT] ALL PASS`
- `[CT] ALL FAIL`

## 4. Что уже автоматизировано

1. Контрактный boot-режим: `scripts/ci/contract_qemu.sh`.
2. `CT-001`, `CT-007`, `CT-008` через contract mode в `init`.
3. `CT-005/CT-006` переведены в `AUTO` после стабилизации wait/lifecycle.
4. Для `CT-003/CT-011` добавлены заготовки contract probes
   (`contract_exec_probe/after`, расширенный contract mode в `init`).
5. Добавлен `CT-013` для регрессии переключения address space (`CR3`) на
   scheduler context switch.
6. `/bin/contract_spawn_wait` и `/bin/contract_fd` доступны как отдельные
   userland-контракты для ручного/аддитивного запуска.
7. `CT-009` частично покрыт обычным boot/userland smoke.

## 5. Минимальный план автоматизации (следующий шаг)

1. Перевести `CT-003/CT-011` в AUTO после стабилизации exec+wait связки.
2. Автоматизировать `CT-004` (ожидание не-дочернего PID).
3. Добавить `CT-012` (cooperative progress через `SYS_NOP` в polling-loop).
4. Перевести `readdir` на handle-based модель и добавить контракт для нового API.
5. Добавить parser/summary отчёт по `[CT]` маркерам.

## 5.1 Исторический критерий разморозки CT-005 (выполнен)

`CT-005` был переведён в `AUTO` после выполнения условий:

1. `waitpid` не зависает на reap-path в contract boot-режиме.
2. `waitpid` корректно возвращает status дочернего `exit`.
3. lifecycle различает завершение и сбор статуса (поведение `ZOMBIE`/collect).
4. 10 последовательных прогонов `scripts/ci/contract_qemu.sh` завершаются PASS
   без таймаутов по `CT-005`.

## 6. Что не считается регрессией

1. Изменение внутренней реализации `spawn/exec/fd`, если CT-инварианты
   сохраняются.
2. Рефакторинг внутренней структуры `fd_table` при сохранении наблюдаемого
   контракта.
3. Изменение внутренней маршрутизации слоя (`kernel/posix` -> `kernel/unix`)
   без нарушения CT-маркеров.

## 7. Правило изменения модели

Любое изменение `docs/ru/unix_process_model.md`, затрагивающее инварианты
`spawn/exec/wait/exit/fd/readdir`, должно сопровождаться обновлением этой
матрицы и статуса соответствующих тестов.
