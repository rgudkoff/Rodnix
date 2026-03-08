# Сборка, запуск, отладка

Этот файл — основной актуальный справочник по сборке и запуску.

## Требования

- `x86_64-elf-gcc` и `x86_64-elf-ld`
- `nasm`
- `qemu-system-x86_64`
- `grub-mkrescue` с BIOS‑модулями GRUB (`i386-pc`)
- `xorriso` и `mtools` (для сборки ISO)

На macOS (Homebrew) проверенный набор:

```bash
brew install qemu x86_64-elf-binutils x86_64-elf-gcc nasm xorriso mtools i686-elf-grub
```

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

Запуск с диагностикой boot/scheduler/usermode:

```bash
make run debug
```

Если QEMU пишет `No bootable device`, проверьте, что ISO содержит BIOS boot image:

```bash
xorriso -indev rodnix.iso -report_el_torito plain
```

В выводе должен быть `El Torito boot img ... BIOS ... /boot/grub/i386-pc/eltorito.img`.

## Отладка (GDB)

```bash
make gdb
qemu-system-x86_64 -m 64M -kernel build/rodnix.kernel -s -S &
gdb build/rodnix.kernel
```

## CI (план)

- Сборка ISO в GitHub Actions.
- Headless QEMU и проверка serial log на “boot ok / shell prompt”.
- Разделение toolchain и ядра, чтобы упростить кэширование.

## Smoke‑test (локально)

```bash
scripts/ci/smoke_qemu.sh
```

Проверка минимального userland/posix пути вручную:

```text
rodnix> run /bin/init
```

Ожидается вывод из userland с `POSIX smoke test start/done` и возврат к `rodnix>`.

Сетевой smoke (путь `init -> spawn /bin/ifconfig -> wait -> sh>`):

```bash
make check-ifconfig-smoke
```
