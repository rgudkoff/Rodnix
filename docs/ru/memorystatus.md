# Memory Pressure и Kills (RodNIX)

Документ фиксирует целевую политику управления дефицитом памяти в RodNIX:
как обнаруживаем pressure, как выбираем действие, кого завершаем и как
делаем поведение воспроизводимым для разработки и отладки.

Базовый референс:
- [xnu/doc/vm/memorystatus_kills.md](https://github.com/apple-oss-distributions/xnu/blob/main/doc/vm/memorystatus_kills.md)

## Зачем это нужно

- Не допускать деградации системы до "необратимого" состояния.
- Разделять мягкие и жесткие действия.
- Сделать OOM/pressure-политику инженерным контрактом, а не набором ad-hoc условий.

## Принципы

- Полный health-check на каждый wakeup (`blind wakeup + full reevaluation`).
- Разделение политики и механики:
  - policy: когда и что делать;
  - action: как именно kill/freeze/notify.
- Решения принимаются по снимку состояния (`system_health_snapshot`).
- Каждое действие имеет `reason`, `band`, `pid`, `freed_estimate` и trace id.

## Приоритеты процессов (jetsam bands)

Вводим bands от наименее важного к наиболее важному:
- `IDLE`
- `BACKGROUND`
- `UTILITY`
- `FOREGROUND`
- `IMPORTANT`
- `CRITICAL`

Общее правило: при pressure завершаем по возрастанию важности band.

## Источники давления (signals)

Минимальный набор сигналов здоровья:
- `available_pages` (свободные страницы).
- `vnode_pressure` (достигнут vnode/inode limit и не удается recycler).
- `zone_pressure` (дефицит VM metadata / VA для allocator).
- `compressor_pressure` (когда компрессор появится).
- `swap_pressure` (когда swap появится).
- `sustained_warning` (долгая warning-фаза без восстановления).

## Классы kill-действий

- `HIGHWATER_SOFT`: процесс превысил soft limit, kill при pressure.
- `TOP_PROCESS`: убить процесс-кандидат с наименьшим приоритетом в допустимых bands.
- `AGGRESSIVE`: расширенный kill-path при thrashing idle-демонов.
- `IDLE_ONLY`: завершать только `IDLE` процессы.
- `PER_PROCESS_HARDLIMIT`: немедленный kill процесса, превысившего hard limit.
- `VNODE_SYNC_KILL`: синхронный kill в контексте потока, который не смог получить vnode.
- `ZONE_SYNC_KILL`: синхронный kill в allocator-path при исчерпании zone VA.
- `SWAPPABLE_KILL`: kill swap-eligible процессов (появится вместе со swap).
- `NO_PAGING_SPACE_ACTION`: специализированное действие при нехватке paging space.

## Матрица причин (XNU -> RodNIX)

Ниже то, что берем как целевую модель из XNU `memorystatus_kills.md`:

| XNU reason | RodNIX статус | Действие |
| --- | --- | --- |
| `MEMORY_HIGHWATER` | P1 | soft-limit kill в pressure-фазе |
| `VNODE` | P2 | синхронный kill по vnode pressure |
| `MEMORY_VMPAGESHORTAGE` | P1 | `TOP_PROCESS` до выхода выше `available_pages_critical` |
| `MEMORY_PROCTHRASHING` | P2 | aggressive kill-path для false-idle |
| `MEMORY_FCTHRASHING` | P3 | kill при file-cache thrashing |
| `MEMORY_PERPROCESSLIMIT` | P1 | немедленный hard-limit kill |
| `MEMORY_DISK_SPACE_SHORTAGE` | P3 | kill frozen/swappable процессов при disk shortage |
| `MEMORY_IDLE_EXIT` | P2 | idle-only kills в warning фазе |
| `ZONE_MAP_EXHAUSTION` | P2 | sync или delegated `TOP_PROCESS` kill |
| `VMCOMPRESSOR_THRASHING` | P3 | targeted kill + reset stats |
| `VMCOMPRESSOR_SPACE_SHORTAGE` | P3 | no-paging-space action / kill |
| `LOWSWAP` | P3 | swappable/suspended-swappable kills |
| `MEMORY_VMPAGEOUT_STARVATION` | P3 | `TOP_PROCESS` kill в pageout starvation |
| `MEMORY_SUSTAINED_PRESSURE` | P2 | периодические `IDLE_ONLY` kills |

## Алгоритм выбора действия (`memorystatus_pick_action` для RodNIX)

1. Собрать `system_health_snapshot`.
2. Если система unhealthy или `available_pages < pressure_threshold`:
   1. попытаться выполнить `HIGHWATER_SOFT`;
   2. если soft-кандидатов нет, проверить условие aggressive thrashing;
   3. иначе выполнить `TOP_PROCESS` с причиной по самому "красному" сигналу.
3. Если warning длится дольше `sustained_warning_window`, включить `IDLE_ONLY`.
4. Повторять цикл до достижения `healthy`.

Инвариант: решение всегда пересчитывается после каждого kill, без кеширования причины.

## Контракт на выбор жертвы

- Никогда не kill-им `CRITICAL` до исчерпания lower bands (кроме hard-limit kill самого `CRITICAL` процесса).
- Внутри одного band выбираем кандидата по score:
  - выше `footprint_bytes`;
  - ниже `recent_user_activity`;
  - выше `relaunch_risk` (если это сервис-loop false-idle);
  - ниже `kill_protection_score`.
- После kill обновляем snapshot и проверяем, достигнут ли recovery target.

## Потоки и точки входа

- `VM_memorystatus_thread`: главный policy loop и асинхронные kills.
- `VM_pressure_thread`: warning/critical transitions, sustained-pressure idle exit.
- `VM_freezer_thread` (P3): freeze/thaw и reclaim из frozen-пула.
- Синхронные точки:
  - vnode allocation path (`VNODE_SYNC_KILL`);
  - zone allocator emergency path (`ZONE_SYNC_KILL`).

## Конфигурация и пороги

Минимальные tunables:
- `available_pages_pressure`
- `available_pages_idle`
- `available_pages_critical`
- `sustained_warning_window_ms`
- `idle_kill_interval_ms`
- `max_idle_kills_per_transition`

Правило: пороги kill-политики не равны user-visible pressure level.

## API и структуры (проектный контракт)

```c
typedef enum {
  RDNX_KILL_NONE = 0,
  RDNX_KILL_HIGHWATER_SOFT,
  RDNX_KILL_TOP_PROCESS,
  RDNX_KILL_AGGRESSIVE,
  RDNX_KILL_IDLE_ONLY,
  RDNX_KILL_PER_PROCESS_HARDLIMIT,
  RDNX_KILL_VNODE_SYNC,
  RDNX_KILL_ZONE_SYNC,
  RDNX_KILL_SWAPPABLE,
  RDNX_KILL_NO_PAGING_SPACE
} rdnx_kill_action_t;
```

```c
typedef enum {
  RDNX_REASON_NONE = 0,
  RDNX_REASON_MEMORY_HIGHWATER,
  RDNX_REASON_VNODE_PRESSURE,
  RDNX_REASON_VM_PAGE_SHORTAGE,
  RDNX_REASON_PROC_THRASHING,
  RDNX_REASON_PER_PROCESS_LIMIT,
  RDNX_REASON_IDLE_EXIT,
  RDNX_REASON_ZONE_EXHAUSTION,
  RDNX_REASON_SUSTAINED_PRESSURE
} rdnx_kill_reason_t;
```

## Наблюдаемость (обязательно)

Счетчики:
- wakeups, health_checks, unhealthy_loops.
- kills_total, kills_by_reason, kills_by_band.
- sync_kills_vnode, sync_kills_zone.
- failed_actions (нет кандидатов / отказ kill).
- recovery_latency_ms (время от unhealthy до healthy).

Логи/trace-события:
- `memorystatus.action_picked`
- `memorystatus.kill`
- `memorystatus.recovery_state_change`

## Тест-план для разработки

1. `highwater`: процесс превышает soft limit, kill при pressure.
2. `critical_shortage`: система ниже critical, серия `TOP_PROCESS` kill.
3. `hardlimit`: мгновенный kill процесса при превышении hard limit.
4. `idle_exit`: sustained warning -> periodic idle kills.
5. `vnode_pressure`: синхронный kill в vnode-path.
6. `zone_pressure`: emergency kill при zone exhaustion.
7. `no_candidate`: корректная деградация и диагностический дамп.

## План внедрения

- P1:
  - `system_health_snapshot`;
  - `HIGHWATER_SOFT` + `TOP_PROCESS`;
  - hard-limit kill;
  - базовые метрики и trace.
- P2:
  - `IDLE_ONLY`, sustained pressure;
  - vnode/zone sync kill-path;
  - thrashing-эвристики.
- P3:
  - freezer/swap/compressor-политики;
  - no-paging-space action;
  - device/profile-specific adaptive thresholds.
