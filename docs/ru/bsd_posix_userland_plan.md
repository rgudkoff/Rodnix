# POSIX/Userland Plan

Этот документ фиксирует, как мы доводим RodNIX до практической
POSIX-совместимости по userland ABI, libc-слою, shell-поведению и базовым
утилитам, поэтапно и без регрессий boot/runtime.

## Целевой принцип

- Основной userland-контракт RodNIX: `POSIX first`.
- RodNIX-specific интерфейсы допустимы только как расширения поверх
  совместимого POSIX baseline.
- Совместимость понимается не как "похожие syscall names", а как максимально
  близкая семантика процессов, fd, путей, сигналов, termios и stdio.

## Что уже импортировано

- Vendor baseline в `third_party/bsd/`:
  - `bin/sh/*` (исходники shell как референс-база)
  - выбранные POSIX заголовки из `include/`
  - `sys/sys/{cdefs.h,queue.h,tree.h}` (локальный снимок)

Важно: этот baseline пока не подключен в сборку Rodnix напрямую.

Текущее состояние интеграции (выполнено):
- В `userland/include/sys/*` поднят минимальный POSIX-слой
  (`errno/fcntl/wait/types/stat`) с выравниванием ключевых числовых
  констант по эталонному baseline.
- В `userland/Makefile` добавлен обязательный шаг `check-bsd-abi`, который
  валидирует соответствие `errno/fcntl/wait` против
  `third_party/bsd/*/sys/sys/*`.

## Почему не подключаем сразу

- Полный `bin/sh` и libc в BSD опираются на широкий слой ABI/CRT/libc.
- Текущий Rodnix userland минималистичен и использует ограниченный набор syscalls.
- Прямое включение без адаптера даст множество несовместимостей.

## Текущее состояние

- Числовые константы `errno/fcntl/wait` уже синхронизируются с BSD baseline.
- Есть минимальный POSIX-shaped syscall ABI и рабочий userspace path.
- Есть `libc-lite`, userspace `init`, `sh` и набор внешних утилит.
- Большая часть syscall-ов и их семантики всё ещё experimental.
- Процессная модель v1 остаётся упрощённой и ещё не дотягивает до
  полноценной POSIX-complete семантики.

## План работ по совместимости

### Этап 1. Зафиксировать POSIX ABI baseline

- Довести числовое и семантическое выравнивание `errno`, `fcntl`, `wait`,
  `stat`, `mman`, `signal`, `termios`, `dirent`, `time`.
- Убедиться, что user-visible return values и `errno` совпадают с ожидаемым
  POSIX/BSD baseline для уже реализованных syscall-ов.
- Убрать временные отклонения, где syscall называется POSIX-именем, но ведёт
  себя не по ожидаемому контракту.

Критерий выхода:
- все публичные constants/flags/structures в `userland/include` проходят
  автоматическую сверку с baseline или явно задокументированы как extension.

### Этап 2. Довести core syscall semantics

- Файлы и пути: `open/close/read/write/stat/fstat/lseek`, `mkdir/unlink/rmdir/rename`,
  `chdir/getcwd`, `readdir/opendir`-path, `fcntl`.
- Процессы: `fork/exec/waitpid/exit`, `FD_CLOEXEC`, inheritance rules,
  `argv/envp` path, zombie/reap semantics.
- Память: `mmap/munmap/brk` с POSIX-ожидаемыми ошибками и базовой совместимой
  семантикой anonymous mappings.
- Каналы и дескрипторы: `pipe/dup/dup2`, корректный lifecycle fd.
- Время и TTY: `clock_gettime`, `nanosleep`, `ioctl`, termios-lite → POSIX-ориентированное поведение.
- Сигналы: `kill/sigaction/sigreturn` с минимально совместимым signal path.

Критерий выхода:
- shell и внешние утилиты проходят типовые Unix-workflow сценарии без
  специальных обходов под RodNIX.

### Этап 3. Расширить libc-lite до практического POSIX shim

- Достроить заголовки `unistd.h`, `fcntl.h`, `sys/stat.h`, `sys/wait.h`,
  `signal.h`, `termios.h`, `dirent.h`, `time.h`, `mman.h`.
- Довести wrappers так, чтобы обычный userspace-код видел привычную POSIX API
  модель: `-1` + `errno`, а не ядровые внутренние коды.
- Укрепить `stdio`, `dirent`, `path`, `getopt`, `pwd/grp` настолько, чтобы
  простые POSIX-утилиты собирались без локальных ad-hoc адаптеров.

Критерий выхода:
- типовые standalone-утилиты и shell helpers собираются против `libc-lite`
  без knowledge о внутренних ABI-деталях ядра.

### Этап 4. Shell и базовые утилиты

- Закрепить shell/utility policy:
  - stateful builtins (`cd`, `exit`, возможно `export`) остаются builtin;
  - `ls/cat/echo/true/...` — внешние исполняемые утилиты.
- Довести shell semantics для pipelines, redirections, exit status, path lookup,
  `exec`, `fork/spawn/wait` interaction.
- Последовательно расширять набор утилит в стиле POSIX userland:
  `true/false`, `echo`, `cat`, `ls`, `cp`, `mv`, `rm`, `grep`, `find`, `sleep`,
  затем следующие по приоритету.
- Rust-утилиты допустимы как часть этой стратегии, если они ведут себя как
  обычные POSIX-утилиты и не вносят отдельный runtime contract.

Критерий выхода:
- интерактивный `/bin/sh` и базовый `/bin` покрывают типовой POSIX-like workflow
  без kernel-shell fallback.

### Этап 5. Совместимость с референсным userland

- Продолжить использовать BSD code/imports как baseline для заголовков,
  shell semantics и expected behavior.
- Подготовить путь к сборке/портированию более референсных userland-компонентов
  поверх `libc-lite`, без прямого vendor-drop-in до готовности ABI.
- Проверять, что RodNIX extensions не мешают сборке и запуску POSIX-oriented кода.

Критерий выхода:
- простые внешние программы, написанные под POSIX/BSD-style headers, требуют
  минимальных или нулевых RodNIX-specific изменений.

### Этап 6. Compatibility test discipline

- Добавить contract/smoke тесты по категориям:
  - process lifecycle;
  - fd inheritance и `CLOEXEC`;
  - path/fs semantics;
  - pipe/redirection/shell;
  - signal delivery;
  - termios/tty behavior;
  - stdio/libc wrappers.
- Для каждого временного отклонения от POSIX держать явный список и тест,
  который позже должен позеленеть без изменения API.

Критерий выхода:
- совместимость становится измеримой: regressions ловятся тестами, а не
  ручной проверкой.

Shell/utility policy (традиционный Unix shell-подход):
- `cd` и другие команды, меняющие состояние самого shell-процесса, остаются builtin;
- `ls/cat/echo/...` — отдельные исполняемые утилиты в userland;
- не дублировать без необходимости одну и ту же команду как builtin и как
  внешний бинарник.

## Что нужно дополнительно добавить из эталонного baseline

Для ближайших шагов (POSIX ABI + libc-lite + shell semantics) нужны как минимум:

- `sys/sys/errno.h`
- `sys/sys/fcntl.h`
- `sys/sys/types.h`
- `sys/sys/stat.h`
- `sys/sys/wait.h`
- `sys/sys/mman.h`
- `sys/sys/unistd.h`
- `sys/sys/signal.h`
- `sys/sys/termios.h`
- `sys/sys/dirent.h`
- `sys/sys/time.h`

(и связанные include-зависимости по месту).

## Ближайший practical POSIX baseline

Для ближайшей практической цели RodNIX должен гарантировать:

- стабильный boot в `userland init -> /bin/sh`;
- корректную семантику `open/read/write/close/stat/lseek`;
- корректную семантику `fork/exec/waitpid/exit`;
- рабочие `pipe/dup/dup2` и shell redirections;
- предсказуемые `errno` и `fcntl` semantics;
- базовые сигналы, `nanosleep`, `clock_gettime`, termios/tty;
- набор внешних утилит, достаточный для типового POSIX-like shell workflow.

## Критерии готовности

- `make` + relevant smoke/contract tests проходят без регрессий.
- userland `init`/`sh` запускаются стабильно как путь по умолчанию.
- документация явно перечисляет поддерживаемый POSIX surface и известные отклонения.
- RodNIX-specific extensions не подменяют и не ломают POSIX baseline.
