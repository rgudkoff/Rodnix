# Userland и bootstrap

## Цели

- Минимальный userland для старта сервисов.
- Bootstrap‑порт и bootstrap‑сервер, раздающий порты сервисов
  (аналог launchd в Darwin).

## Bootstrap‑сервер (MVP)

- Запускается одним из первых userland процессов.
- Принимает запросы на регистрацию сервисов.
- Выдает порты сервисов по имени/типу.

## Статус (сейчас)

- В репозитории есть каркас bootstrap‑сервера в `userland/bootstrap/`.
- Есть минимальный ELF‑loader и запуск ring3 через `loader_exec()`.
- Syscall boundary (0x80) активен, есть базовые POSIX‑syscalls.
- В ядре зарезервирован bootstrap‑порт (placeholder), протокола нет.
- Есть временный kernel‑mode bootstrap server (thread), отвечающий статусом `0`.
- Есть минимальный loader‑stub в ядре (ELF64 ET_EXEC, PT_LOAD).
- Добавлена базовая инфраструктура ring3 (GDT user‑сегменты + TSS RSP0) и тестовый user‑stub.
- Введён отдельный PML4 для user‑stub (ядро мапится в higher‑half).
- Initrd поддерживается как источник файлов (`/bin/init`).

## Инварианты

- Все пользовательские сервисы получают порты через bootstrap.
- Прямые "захардкоженные" порты запрещены.

## Где смотреть в коде

- `userland/` и `userland/bootstrap/`.
- `kernel/common/loader.c` и `kernel/arch/x86_64/usermode.c`.
- `scripts/mkinitrd.py`, `boot/grub/grub.cfg`.
