# Framebuffer Ownership Spec

## Scope

Этот документ задаёт минимальный контракт для графической подсистемы RodNIX
вокруг `/dev/fb0`, onscreen ownership и возврата текстовой консоли.
Цель: дать стабильную основу для raw framebuffer userspace, не ломая kernel
text console.

## Objects

- `/dev/console`
  Текстовый терминал. Отвечает за символы, ANSI, курсор, scroll, prompt,
  line discipline.
- `/dev/fb0`
  Raw framebuffer device. Отвечает только за пиксельный доступ и metadata
  framebuffer.
- `gfx_console`
  Onscreen renderer для `/dev/console`, работающий только когда text console
  владеет экраном.
- `fb client`
  Userspace-процесс, который получил ownership экрана через framebuffer API.

## Core Rule

В каждый момент времени существует ровно один `active onscreen owner`.

Допустимые владельцы:

- `TEXT_CONSOLE`
- `FRAMEBUFFER_CLIENT`

Kernel logging не зависит от `active onscreen owner`.

## Ownership States

1. `BOOT_CONSOLE`
   Ранний режим до полной инициализации GFX/TTY.
2. `TEXT_CONSOLE`
   Экран принадлежит текстовой консоли. `gfx_console` рисует onscreen terminal.
3. `FRAMEBUFFER_CLIENT`
   Экран принадлежит userspace framebuffer client. `gfx_console` не рисует
   onscreen.
4. `HEADLESS_FALLBACK`
   Опциональное внутреннее состояние, если display временно недоступен.
   Логи остаются в serial/VGA.

Для MVP можно реально использовать только:

- `TEXT_CONSOLE`
- `FRAMEBUFFER_CLIENT`

## Device Roles

`/dev/console`:

- принимает текстовый вывод
- интерпретирует terminal semantics
- поддерживает ANSI/cursor/colors
- не предоставляет raw pixel API

`/dev/fb0`:

- возвращает metadata framebuffer
- позволяет `mmap()` framebuffer memory
- не обязан поддерживать terminal semantics
- не обязан смешивать onscreen text console с userspace graphics

## Framebuffer Metadata ABI

`read(fd, &fb_dev_info_t, sizeof(fb_dev_info_t))` возвращает framebuffer
descriptor.

Минимально стабильные поля:

- `width`
- `height`
- `pitch`
- `bpp`
- `size`
- `phys_base`
- `pixel_format`

Рекомендуется считать `pixel_format` обязательной частью ABI.

Примеры:

- `FB_PIXFMT_XRGB8888`
- `FB_PIXFMT_RGB565`

Если структура меньше/больше ожидаемой, ядро должно использовать
versioned/size-aware semantics либо жёстко фиксированный ABI v1.

## mmap Contract

`/dev/fb0` поддерживает `mmap(PROT_READ|PROT_WRITE, MAP_SHARED)`.

Правила:

- `offset` для MVP должен быть только `0`
- размер не должен превышать `fb_dev_info_t.size`
- mapping даёт доступ к framebuffer memory
- mapping сам по себе не обязан означать ownership экрана

Если сохраняется временный autograb:

- это должно быть явно documented как MVP behavior
- долгосрочный контракт всё равно должен считать ownership отдельной
  операцией

## Ownership API

Рекомендуемый интерфейс:

- `FB_IOC_ACQUIRE`
- `FB_IOC_RELEASE`
- опционально `FB_IOC_QUERY_OWNER`

Семантика:

- `FB_IOC_ACQUIRE`
  пытается сделать caller активным onscreen owner
- `FB_IOC_RELEASE`
  освобождает ownership caller-а
- `FB_IOC_QUERY_OWNER`
  возвращает текущий owner/type state

Для MVP:

- первый успешный acquire wins
- второй клиент получает `EBUSY`
- force-acquire не поддерживается

## Ownership Lifetime

Ownership не должен храниться как простой `bool`.

Нужна модель lifetime:

- ownership refcount
- владелец привязан к `task/process`, а не просто к факту существования
  `mmap`
- release происходит:
  - явно через `FB_IOC_RELEASE`
  - при закрытии последнего owning handle, если ownership handle-based
  - при `munmap`, если mapping участвует в ownership model
  - обязательно при `task/process exit`

Рекомендуемая модель:

- ownership принадлежит `task/process`
- `open()` ownership не даёт
- `mmap()` ownership не даёт по чистому контракту
- ownership даёт только `FB_IOC_ACQUIRE`

Если временно сохраняется autograb on mmap:

- он должен внутренне трансформироваться в ownership ref
- этот ref обязан сниматься при `munmap` и `task destroy`

## Kernel Logging Policy

Kernel logs никогда не должны зависеть от framebuffer ownership.

При `FRAMEBUFFER_CLIENT`:

- logs продолжают идти в serial
- logs могут продолжать идти в VGA text path
- logs не должны рисоваться поверх onscreen framebuffer, если owner =
  userspace

Это обязательное правило, чтобы raw graphics client не получал случайный
onscreen corruption от kernel messages.

## Text Console Visibility Policy

`/dev/console` и `/dev/fb0` не могут одновременно быть активными onscreen
renderers.

Допускается:

- `/dev/console` логически продолжает существовать как TTY
- но его GFX backend не рисует onscreen, если owner = `FRAMEBUFFER_CLIENT`

## Return To Text Console

После освобождения framebuffer ownership:

1. `active onscreen owner` переключается на `TEXT_CONSOLE`
2. GFX terminal backend reattaches/re-enables
3. выполняется полный redraw terminal state
4. курсор возвращается в корректную позицию
5. prompt и текущее содержимое терминала должны быть видимы без повторной
   генерации вывода приложением

Это означает, что text console должна иметь собственный screen/cell buffer, а
не быть только immediate-render path.

## Terminal State Requirement

Чтобы `TEXT_CONSOLE` можно было корректно восстановить после
`FRAMEBUFFER_CLIENT`, текстовая консоль обязана хранить:

- символы на экране
- fg/bg атрибуты
- cursor position
- terminal state, достаточный для redraw

Без этого release framebuffer ownership не сможет корректно вернуть консоль.

## Concurrency / Multi-client Rules

Для MVP:

- только один framebuffer owner одновременно
- остальные acquire получают `EBUSY`
- множественные `mmap()` без ownership допустимы только если это явно
  разрешено policy
- если raw mapping без ownership разрешён, такой клиент не получает onscreen
  guarantees

## Error Semantics

- `open("/dev/fb0")` succeeds, если framebuffer зарегистрирован
- `read()` возвращает metadata или `EINVAL` при неверном размере
  буфера/контракте
- `mmap()` возвращает `EINVAL` при неверном offset/size/protection
- `FB_IOC_ACQUIRE` возвращает `EBUSY`, если экран уже захвачен другим
  клиентом
- `FB_IOC_RELEASE` возвращает ошибку, если caller не владелец

## Non-goals For MVP

На этом этапе не требуется:

- compositor
- multiple onscreen layers
- alpha blending между console и fb client
- VT switching
- hotplug display routing
- multiple framebuffer owners
- advanced modesetting ABI

## Recommended MVP Policy

1. `/dev/console` — единственный onscreen renderer по умолчанию
2. `/dev/fb0` — raw graphics API
3. ownership экрана получает только `FB_IOC_ACQUIRE`
4. ownership снимается через `FB_IOC_RELEASE` и автоматически при смерти
   владельца
5. kernel logs всегда остаются в serial/VGA
6. после release экран возвращается текстовой консоли с полным redraw

## Versioning Note

Если ABI `/dev/fb0` уже начинает использоваться userspace-кодом, желательно
как можно раньше зафиксировать:

- версию `fb_dev_info_t`
- enum `pixel_format`
- ownership ioctls
- политику `mmap != ownership`

Это минимизирует будущие несовместимости.
