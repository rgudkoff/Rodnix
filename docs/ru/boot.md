# Загрузка и ранний старт

## Цели подсистемы

- Надежный вход по Multiboot2.
- Минимальный 32-битный stub.
- Переход в x86_64 long mode.
- Базовая подготовка окружения для ядра.

## Текущий путь загрузки (x86_64)

- Загрузка через Multiboot2 (GRUB).
- 32-битный входной stub подготавливает CPU.
- Переход в long mode и передача управления ядру.
- Инициализация базовых таблиц и прерываний.

## Карта процесса запуска (последовательность функций)

Ниже — сквозная последовательность от входа ядра до запуска shell, с ключевыми
переходами. Это фиксирует текущий порядок и помогает сверять изменения.

1. `boot/*` (32-bit stub)
   - Подготовка CPU и переход в long mode.
   - Передача управления в `kmain()`.

2. `kmain()` (`kernel/main.c`)
   - `console_init()`, `console_clear()`, ранний вывод.
   - `boot_early_init(&boot_info)` — ранний сбор boot-информации.
   - `kernel_run_bootstrap_sysinit()` — staged init подсистем:
     `cpu -> interrupts -> memory -> apic/acpi -> timer -> sched -> ipc ->
     syscall -> security -> loader -> kmod -> fabric/gfx -> vfs -> net`.
   - `kernel_enable_runtime_interrupts()` — включение IRQ после завершения
     bootstrap-phase.
   - `kernel_enter_runtime()` — выбор userspace `init` или fallback в kernel shell,
     создание primary/idle потоков и handoff к планировщику.
   - `scheduler_start()` — запуск планировщика и первый switch.

3. IRQ 32 (таймер)
   - `irq_handler()` → `interrupt_dispatch()`.
   - `scheduler_tick()` — установка resched.
   - `scheduler_switch_from_irq()` — выбор первого runnable потока.

4. Первый поток (shell)
   - IRETQ на `thread_trampoline()`.
   - `thread_trampoline()` включает IRQ и вызывает `shell_run()`.
   - `shell_run()` выводит приветствие и prompt, затем ждёт ввод.

## Входные данные

- Структуры Multiboot2, в том числе карта памяти.
- Базовая информация о загрузке (см. код в `boot`).
- Initrd передается как Multiboot2 module (type 3).

## Инварианты

- До перехода в long mode нельзя вызывать 64-битные пути.
- Адреса, используемые в ранней фазе, должны быть явно описаны.
- Карта памяти Multiboot2 — источник истины для PMM.

## Логирование загрузки (слоистый подход)

В RodNIX внедрен минимальный слоистый подход к boot-логированию:

- Есть единая точка для отметок фаз: `bootlog_mark(phase, event)`.
- Канал 1 (всегда): событие пишется в внутренний ring через `debug_event`
  в формате `boot:<phase>:<event>`.
- Канал 2 (опционально): человекочитаемый вывод в консоль
  `[BOOT][seq] phase: event`.

Это повторяет базовую идею двухканального старта:
- human-readable boot messages (`kprintf/printf`);
- ранний машинный канал событий.

### Где в коде

- Реализация: `trace/bootlog.c`
- Интерфейс: `trace/bootlog.h`
- Стадии инициализации: `init/sysinit.c`, `init/runtime.c`
- Точка входа: `kernel/main.c`

### Формат V2 (структурированный)

Человекочитаемый канал:
- `[BOOT2] seq=<n> ph=<id> ev=<id> cpu=<id> tk=<ticks> p=<phase> e=<event>`
- `[DEGRADED] ...` — явная отметка режима с fallback (например, legacy PIC IRQ).

Ring-событие (`debug_event`):
- `boot2 seq=<n> ph=<id> ev=<id> cpu=<id> tk=<ticks> p=<phase> e=<event>`

Поля фиксированы:
- `seq` — порядковый номер boot-события.
- `ph` — числовой `phase_id`.
- `ev` — числовой `event_id`.
- `cpu` — идентификатор CPU.
- `tk` — `scheduler ticks` на момент события.
- `p` / `e` — строковые имена фазы и события (для читаемости).

Примечание по форматам:
- В ядре `%x/%llx` уже печатаются с префиксом `0x`, поэтому в сообщениях не нужно
  добавлять дополнительный литерал `0x` перед `%x/%llx`.

### Идентификаторы фаз (`phase_id`)

- `1` startup
- `2` kmain
- `3` boot
- `4` cpu
- `5` interrupts
- `6` memory
- `7` apic
- `8` timer
- `9` scheduler
- `10` ipc
- `11` syscall
- `12` security
- `13` loader
- `14` fabric
- `15` vfs
- `16` net
- `17` shell
- `18` threads

### Идентификаторы событий (`event_id`)

- `1` mark
- `2` enter
- `3` done
- `4` fail
- `5` start
- `6` enable_enter
- `7` enable_done
- `8` kernel_ready
- `9` created
- `10` bootlog_init
- `11` lapic
- `12` pit
- `13` fallback_pic
- `14` kernel_task_fail
- `15` thread_create_fail
- `16` create_enter

### Boot-аргументы

Полный актуальный список и примеры вынесены в отдельный документ:
- `boot_args.md`

По умолчанию человекочитаемый boot-log включен.

Включить явно:
- `startup_debug=1`
- `startup_debug=verbose`
- `bootlog=verbose`

Отключить человекочитаемый канал:
- `startup_debug=0`
- `bootlog=quiet`

Компактные события `boot:<phase>:<event>` в ring пишутся всегда.

Режим bootstrap (shell не часть ядра по умолчанию):
- `rdnx.init=/bin/init` — путь к первому userspace процессу (по умолчанию `/bin/init`).
- `rdnx.shell=1` — запуск встроенного kernel shell вместо userspace init.

Если запуск `rdnx.init` не удался, ядро включает fallback:
- `[DEGRADED] userland init unavailable, starting kernel shell fallback`.

## Runtime Trace V2 (scheduler/memory/fault)

Помимо boot-фаз добавлен унифицированный runtime emitter:

- консоль: `[TR2] s=<seq> c=<cat> e=<ev> cpu=<id> tk=<ticks> a0=<v> a1=<v>`
- ring: `tr2 s=<seq> c=<cat> e=<ev> cpu=<id> tk=<ticks> a0=<v> a1=<v>`

Где:
- `s` — локальный sequence runtime-событий,
- `c` — категория,
- `e` — событие в категории,
- `a0/a1` — payload (числовые аргументы).

### Категории (`c`)

- `1` boot
- `2` scheduler
- `3` memory
- `4` fault

### События scheduler (`c=2`)

- `1` block (`a0=tid`, `a1=state`)
- `2` switch (`a0=prev_tid`, `a1=next_tid`)
- `3` reaper_overflow (`a0=queue_len`, `a1=dropped`)
- `4` exit (`a0=tid`, `a1=task_id`)

### События memory (`c=3`)

- `1` init_enter
- `2` init_done (`a0=free_pages`, `a1=used_pages`)
- `3` init_fail (`a0=stage_id`)

### События fault (`c=4`)

- `1` exception (`a0=vector`, `a1=error_code`)
- `2` page_fault (`a0=cr2`, `a1=rip`)

## Инварианты входа в 64‑битный C

Ниже приведены обязательные условия, которые должны выполняться к моменту
входа в `kmain()`:

1. Карта виртуальной памяти (минимум).
Есть identity map низких адресов (минимум 1 ГиБ) для раннего кода.
Есть higher‑half direct‑map ядра: `KERNEL_VMA_BASE + phys`.
Код/данные ядра доступны по виртуальным адресам линковки.

2. Гарантированные маппинги.
Текст/данные ядра.
Стек раннего 64‑битного кода.
Таблицы страниц, используемые для входа в long mode.
VGA (`0xB8000`) через physmap.
MMIO окна (APIC/IOAPIC) после явного маппинга.

3. Таблицы страниц.
Таблицы страниц лежат в низкой памяти и доступны по identity map.
Доступ к ним в 64‑битном коде происходит через physmap.
Перед отключением identity map обязателен стабильный physmap.

4. Состояние CPU.
Long mode включён (EFER.LME + CR0.PG).
SSE/FXSR включены перед входом в C‑код.
Корректный GDT/CS, валидные сегменты данных.

## Планы

- Стабильный higher-half mapping.
- Четкая граница между ранним и основным этапами инициализации.

## Где смотреть в коде

- `boot` и `kernel/arch/x86_64`.
