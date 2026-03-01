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
   - `console_init()`, `console_clear()`, приветственный лог.
   - `boot_early_init(&boot_info)` — ранний сбор boot-информации.
   - `cpu_init()` — базовая CPU-инициализация.
   - `interrupts_init()` — IDT, PIC, очистка таблицы обработчиков.
   - `memory_init()` — paging + PMM bootstrap.
   - `apic_init()` — LAPIC + попытка IOAPIC.
   - `apic_timer_init(100)` или `pit_init(100)` — таймер.
   - `scheduler_init()` — минимальная инициализация планировщика.
   - `ipc_init()` — базовая IPC-подсистема.
   - `syscall_init()` — минимальный каркас syscalls.
   - `fabric_init()` — устройство/шина (virt, pci, ps2).
   - `hid_kbd_init()` — клавиатура и IRQ маршрутизация.
   - `vfs_init()` — RAMFS/VFS.
   - Разрешение прерываний (`sti`) и старт таймера.
   - `shell_init()` — подготовка shell.
   - Создание потоков: `shell_thread`, `idle_thread`.
   - `scheduler_add_thread()` для shell и idle.
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

## Логирование загрузки (XNU-inspired)

В RodNIX внедрен минимальный XNU-подход к boot-логированию:

- Есть единая точка для отметок фаз: `bootlog_mark(phase, event)`.
- Канал 1 (всегда): событие пишется в внутренний ring через `debug_event`
  в формате `boot:<phase>:<event>`.
- Канал 2 (опционально): человекочитаемый вывод в консоль
  `[BOOT][seq] phase: event`.

Это повторяет идею XNU:
- human-readable boot messages (`kprintf/printf`);
- ранний машинный канал событий (`kernel_debug_string_early` в XNU).

### Где в коде

- Реализация: `kernel/common/bootlog.c`
- Интерфейс: `kernel/common/bootlog.h`
- Использование в пути старта: `kernel/main.c`

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

По умолчанию человекочитаемый boot-log включен.

Включить явно:
- `startup_debug=1`
- `startup_debug=verbose`
- `bootlog=verbose`

Отключить человекочитаемый канал:
- `startup_debug=0`
- `bootlog=quiet`

Компактные события `boot:<phase>:<event>` в ring пишутся всегда.

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
