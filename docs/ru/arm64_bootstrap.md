# ARM64 bootstrap

Этот документ фиксирует минимальную базу для вывода RodNIX на `arm64`.

## Что уже заложено

- архитектура выбирается через `ARCH=...` в корневом `Makefile`;
- общие include-точки вынесены в `kernel/arch/*.h`;
- общие участки ядра начали использовать `ARCH_*` макросы вместо прямой привязки к `x86_64`;
- `kernel/Makefile` и `boot/Makefile` теперь разделяют архитектурные source-листы.
- `userland/build` разведен по архитектурам: `userland/build/<arch>`.

## Что это пока не означает

- `arm64` ещё не является рабочей сборкой ядра;
- boot flow, MMU/PMM, usermode, syscall fast path и platform interrupt controller для `arm64`
  пока не реализованы;
- userland toolchain и ABI-стек всё ещё ориентированы на текущий `x86_64` путь.
- `userland/rootfs` пока остается общим staging-каталогом, то есть это еще не полностью
  независимый multi-arch userspace pipeline.

## Следующие шаги

1. Ввести `arm64`-версии `paging.h`, `pmm.h`, `interrupt_frame.h`, `usermode.h`.
2. Развести x86-специфичные поля `interrupt_frame_t` и signal/save-restore path.
3. Убрать оставшиеся `X86_64_*` обращения из общих `vm`, `loader`, `main`, `unix`.
4. Добавить `arm64` boot entry и минимальный линкерный/layout путь.
5. Отдельно параметризовать `userland/Makefile` и `crt0.S` под новую ABI/ISA.
