# Сборка, запуск, отладка

Этот файл — краткий справочник. Подробности на английском: `docs/BUILD.md`.

## Требования

- `x86_64-elf-gcc` и `x86_64-elf-ld`
- `nasm`
- `qemu-system-x86_64`
- `grub-mkrescue` (для ISO)

## Сборка

```bash
make clean
make
```

## ISO и запуск

```bash
make iso
make run
```

## Отладка (GDB)

```bash
make debug
qemu-system-x86_64 -m 64M -kernel build/rodnix.kernel -s -S &
gdb build/rodnix.kernel
```

## Где смотреть

- `docs/BUILD.md` для расширенных инструкций.
