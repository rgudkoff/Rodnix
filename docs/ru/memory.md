# Память (PMM/VM)

## Цели

- Корректное управление физической памятью по карте Multiboot2.
- Базовый VM-стек с разделением уровней `vm_map -> vm_object -> pmap`.
- Прозрачная связка между PMM и VMM.
- Предсказуемое поведение page fault / COW / OOM.

## Что уже есть

- Парсинг карты памяти Multiboot2 при загрузке.
- Физический менеджер памяти на битовой карте (ранняя версия).
- Базовый `pmap`/paging для x86_64 (`create_user_pml4`, map/unmap/switch CR3).
- Каркас VM-слоя в стиле FreeBSD/XNU:
  - `vm_map` (таблица регионов процесса),
  - `vm_object` (жизненный цикл backing object),
  - `vm_pager` (zero-fill страница для demand path),
  - `vm_fault_handle` (user page fault recheck/map path).
- Минимальные POSIX точки входа:
  - `mmap/munmap/brk` (анонимная + file-backed память, lazy allocation на page fault).
- `fork` v1 через clone `vm_map` и COW-entries:
  - shared object + write-fault split для private writable mappings.

## Что планируется (кратко)

- PMM v2 с зонами и поддержкой дыр в адресном пространстве.
- VM map для процесса/ядра (regions + protections + inheritance).
- VM object как источник страниц (анонимная память/файл/zero-fill).
- Fault path: lookup map -> resolve object -> pmap enter -> retry.
- Базовый pager-интерфейс (backing object), не привязанный к конкретной FS.
- COW для `fork` и map shadow-цепочек.
- Wired/pinned memory для критичных подсистем (IRQ/IO paths).
- API для снимка регионов PMM, пригодного для VM.
- Базовый kernel heap и простейший аллокатор.

## Инварианты

- Нельзя использовать фиксированные границы памяти без данных MB2.
- Все неиспользуемые или неизвестные регионы считаются зарезервированными.
- Аллокации должны быть воспроизводимы и диагностируемы.
- `pmap` не владеет политикой VM, только аппаратным отображением.
- Все user pointer доступы проходят через fault-safe путь.
- Любой page state имеет единственный владелец и валидный переход состояния.

## Модель памяти

Историческое формальное описание владения/OOM: `docs/ru/archive/memory_model.md`.

## Контракты

- PMM предоставляет выделение/освобождение физических страниц.
- VM map предоставляет region lookup/protection/inheritance.
- VM object предоставляет жизненный цикл страниц и backing-store семантику.
- pmap предоставляет отображение физической страницы в виртуальную.
- Оба уровня должны иметь минимальные диагностические счетчики.

## Fault path (целевой)

1. Trap page fault и валидация контекста (kernel/user, access type).
2. Lookup `vm_map_entry` по виртуальному адресу.
3. Проверка прав (`r/w/x`) и COW-условий.
4. Получение страницы через `vm_object` (resident/allocate/fill-from-pager).
5. `pmap_enter` и TLB maintenance.
6. Возврат в поток или сигнал/ошибка при нерешаемом fault.

## Источник архитектурного подхода

Для целевого дизайна используем подходы из документации XNU `doc/vm`:
[xnu/doc/vm](https://github.com/apple-oss-distributions/xnu/tree/main/doc/vm)

Исторические заметки по memory pressure policy:
`docs/ru/archive/memorystatus.md`.

## Где смотреть в коде

- `kernel/common` и `kernel/arch/x86_64`.
- Актуальный план — `execution_plan_os_foundation.md`.
