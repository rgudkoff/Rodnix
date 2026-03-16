# P0 Focus Plan (стабилизация)

Актуально на 8 марта 2026.

Цель P0: довести текущий рабочий путь
`boot -> /bin/init -> /bin/sh`
до стабильного и повторяемого baseline без panic/hang и ABI-хаоса.

## Поток 1: Syscall namespace и ABI freeze

- Зафиксировать правило: `SYS_*` (legacy) только для совместимости ядра,
  публичный userspace ABI идёт через `POSIX_SYS_*`.
- Исключить коллизии номеров между legacy и POSIX пространствами.
- Поддерживать единственный источник истинных номеров:
  `kernel/posix/syscalls.master`.
- Вести статус syscall-ов: `stable`/`experimental`.

Критерий готовности:
- Нет пересечений legacy/POSIX номеров.
- Таблица в документации совпадает с `syscalls.master`.

## Поток 2: Process lifecycle cleanup

- Закрыть teardown-path после `exit` для task/thread/address_space.
- Проверить повторный запуск `run /bin/init` без накопления утечек.
- Убрать ветки, где shell/user thread может зависнуть на ошибке.

Критерий готовности:
- 100 прогонов smoke-петли локально без panic/hang.
- Нет роста deferred/reaper queue в steady state.

## Поток 3: User pointer safety baseline

- Унифицировать проверки user pointers в обработчиках syscall-ов.
- Явно описать поведение для invalid pointers/fault-path.
- Добавить негативные smoke-тесты (bad pointer, oversize length, bad cstr).

Критерий готовности:
- Некорректные указатели детерминированно возвращают ошибку, без panic.

## Поток 4: CI smoke gate

- Ввести обязательный headless smoke в CI:
  `boot -> userspace init completed`.
- Сделать smoke-gate блокирующим для merge в основную ветку.
- Хранить boot-лог как артефакт падения.

Критерий готовности:
- Каждый PR проходит автоматический smoke без ручного запуска QEMU.

## Очередность выполнения

1. Поток 1 (ABI freeze) — сразу, чтобы не накапливать долг по интерфейсам.
2. Поток 4 (CI gate) — сразу после потока 1, чтобы ловить регрессии.
3. Поток 2 (lifecycle) — основной риск стабильности.
4. Поток 3 (pointer safety) — закрывает класс аварий/коррупции.

## Definition of Done для P0

- Стабильный smoke-путь в CI и локально.
- Зафиксированная таблица syscall номеров и статусов.
- Предсказуемый teardown процессов/потоков.
- Детерминированное поведение на невалидных user pointers.
