# Boot-аргументы RodNIX

Этот документ фиксирует **поддерживаемые сейчас** boot-аргументы ядра RodNIX
и их фактическое поведение.

## Где задаются

Аргументы передаются в строке `multiboot2` (GRUB):

```cfg
menuentry "RodNIX" {
    multiboot2 /boot/rodnix.kernel rdnx.init=/bin/init bootlog=verbose
    module2 /boot/initrd.img
    boot
}
```

## Поддерживаемые аргументы

1. `rdnx.init=<path>`
- Назначение: путь к первому userspace-процессу (init).
- Значение по умолчанию: `/bin/init`.
- Пример: `rdnx.init=/sbin/init`.
- Поведение: ядро создаёт bootstrap-thread и вызывает `loader_exec(<path>)`.

2. `rdnx.shell=1`
- Назначение: принудительно включить встроенный kernel shell (debug mode).
- Поведение: userspace init не запускается, стартует kernel shell.

3. `shell=1`
- Назначение: legacy-алиас для `rdnx.shell=1`.
- Поведение: то же, что и `rdnx.shell=1`.

4. `bootlog=verbose`
- Назначение: включить человекочитаемый `BOOT2` лог.

5. `bootlog=quiet`
- Назначение: выключить человекочитаемый `BOOT2` лог.

6. `startup_debug=1`
7. `startup_debug=verbose`
- Назначение: включить человекочитаемый `BOOT2` лог (эквивалент `bootlog=verbose`).

8. `startup_debug=0`
- Назначение: выключить человекочитаемый `BOOT2` лог (эквивалент `bootlog=quiet`).

## Bootstrap-поведение

По умолчанию:
- запускается userspace init (`rdnx.init`, default `/bin/init`).

Если userspace init не запустился:
- ядро пишет `[DEGRADED] userland init unavailable, starting kernel shell fallback`;
- запускается встроенный kernel shell как fallback.

Если задан `rdnx.shell=1`:
- ядро сразу запускает kernel shell и не пытается стартовать userspace init.

## Примеры

1. Обычная загрузка userspace init с подробным логом:

```cfg
multiboot2 /boot/rodnix.kernel rdnx.init=/bin/init bootlog=verbose
```

2. Минимальный лог (тише) и стандартный init:

```cfg
multiboot2 /boot/rodnix.kernel bootlog=quiet
```

3. Принудительный kernel shell:

```cfg
multiboot2 /boot/rodnix.kernel rdnx.shell=1
```

