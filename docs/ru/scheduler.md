# Планировщик (целевой дизайн)

## Цели

- Низкая латентность интерактивных задач без starvation фоновых.
- Предсказуемость под нагрузкой и наблюдаемость через метрики.
- Эволюция от текущего MLQ к иерархической модели `bucket -> group -> thread`.

## Источник архитектурного подхода

При проектировании используем идеи из XNU `sched_clutch`/`sched_clutch_edge`:
[sched_clutch_edge.md](https://github.com/apple-oss-distributions/xnu/blob/main/doc/scheduler/sched_clutch_edge.md)

Это не копирование реализации, а перенос принципов:
- приоритизация по QoS bucket,
- fairness между группами потоков,
- интерактивные окна (warp/starvation avoidance),
- отдельная политика для top-level выбора и thread-level time sharing.

## Текущий статус RodNIX (v0)

- MLQ с 3 уровнями (low/normal/high).
- TIMESHARE/REALTIME + динамический приоритет для TIMESHARE.
- boost при пробуждении, penalty для CPU-bound, clamping.
- базовое IPC priority inheritance (best-effort).
- deferred reaper + отдельный reaper-thread + базовые метрики reaper/stack-cache.

## Целевая модель (v1-v2)

### 1) Bucket level (QoS)

- Ввести QoS bucket-и: `BACKGROUND`, `UTILITY`, `DEFAULT`, `INTERACTIVE`.
- Выбор bucket делать не только по статическому приоритету, а по дедлайну.
- Использовать EDF-подобный выбор bucket с latency budget (аналог WCEL).

Инвариант:
- high-QoS имеет lower latency target, но не может бесконечно вытеснять low-QoS.

### 2) Group level (Thread Group)

- Ввести `thread_group` (обычно process/service group).
- Планировать справедливость между группами в пределах bucket.
- Учитывать интерактивность группы: доля blocked/wakeup IPC vs CPU-bound.

Инвариант:
- один CPU-heavy процесс не должен выдавливать остальные группы того же bucket.

### 3) Thread level

- Внутри группы оставить decay/boost модель TIMESHARE.
- Поддержать inheritance donation/rollback для IPC-цепочек.
- Квант делать bucket-aware: интерактивные короче, фоновые длиннее.

## Окна для латентности и anti-starvation

### Warp window

- Короткое окно для резкого повышения интерактивной задачи после wakeup/IPC.
- Ограничение по времени и частоте, чтобы не разрушать fairness.

### Starvation avoidance window

- Если bucket/group долго не получает CPU, выделяется гарантированный слот.
- Метрика starvation фиксируется в статистике планировщика.

## Старт первого потока

Первый запуск инициируется через программный `int $32` в `scheduler_start()`,
чтобы использовать тот же IRQ-путь, что и обычная преэмпция.

Порядок:
1. `scheduler_start()` выставляет `resched_pending = true`.
2. Выполняется `int $32`.
3. IRQ32 вызывает `scheduler_tick()` + `scheduler_switch_from_irq()`.
4. Возвращается `interrupt_frame_t*` выбранного runnable-потока.

## Пошаговое внедрение

1. `v1`:
- Зафиксировать структуру `thread_group` и bucket enum.
- Добавить метрики latency/fairness/starvation.
- Вынести выбор bucket в отдельный этап (до MLQ выбора потока).

2. `v1.5`:
- Включить warp window и starvation avoidance window.
- Добавить per-bucket quantum policy.

3. `v2`:
- EDF-подобный bucket scheduling с budget/deadline.
- Расширенное inheritance для IPC-цепочек.

## Инварианты

- Контекст-свитч сохраняет/восстанавливает полный набор регистров.
- Решение планировщика детерминировано по фиксированным входам.
- Любой bucket/group получает CPU в ограниченное время (bounded starvation).

## Наблюдаемость

Обязательные счётчики:
- `context_switches`, `preemptions`, `voluntary_yields`,
- per-bucket runtime и wait-time,
- starvation events и warp activations,
- reaper queue stats, stack-cache stats.

## Где смотреть в коде

- `kernel/common/scheduler/` (модули: `state/runqueue/control/tick/switch/reaper/debug`)
- `kernel/common/task.c`
- `kernel/common/ipc.c`
