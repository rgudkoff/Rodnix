# RodNIX Process Model v1

Дата: 2026-03-08  
Статус: active

Этот документ фиксирует единую правду для `kernel/unix`:

- `spawn` как базовый примитив создания процесса;
- `exec` как замена образа текущего процесса;
- инварианты lifecycle, inheritance и reap.

## 1. Объявленная модель

RodNIX v1: **Unix-like simplified model**.

- `spawn` — primary process creation primitive.
- `exec` — replace current process image.
- `fork` — intentionally absent in v1.

Важно: это не "почти классический Unix", а явно ограниченная модель v1.

## 2. Контракт `spawn`

`spawn(path, argv, envp?)` в терминах семантики v1:

1. Создаёт **новый PID**.
2. Создаёт **новое адресное пространство** дочернего процесса.
3. Загружает указанный образ программы.
4. Инициализирует начальные `argv` (и позже `envp` при поддержке).
5. Возвращает PID дочернего процесса при успехе.

Родительство:

- родитель = процесс, вызвавший `spawn`;
- `exec` не меняет parent/child связи.

## 3. Контракт `exec`

`exec(path, argv, envp?)` в терминах семантики v1:

1. **Не меняет PID**.
2. Полностью заменяет user image текущего процесса.
3. Очищает старый user memory image (старый образ больше не исполняется).
4. Применяет правила наследования/закрытия дескрипторов.
5. Не создаёт новый process object.

## 4. Что сохраняется/сбрасывается при `exec`

Ниже — **нормативные** правила v1 для развития подсистем.

Сохраняется:

1. `pid`, `ppid`.
2. parent/child links.
3. открытые `fd`, кроме помеченных `close-on-exec` (когда флаг активен).
4. `cwd` (и `root dir`, если модель будет добавлена).
5. uid/gid/euid/egid (когда security model включена).

Сбрасывается:

1. old user memory mappings образа.
2. old argv/env image state.
3. user stacks старого образа.
4. image-specific user state (TLS и т.п., при появлении соответствующего механизма).

Многопоточность (forward contract):

- при появлении multithreaded process model `exec` должен оставлять
  вызывающий контекст и сбрасывать остальные user threads.

## 5. Инварианты Unix-семантики

Lifecycle:

1. `spawn` создаёт новый process object (не заменяет текущий).
2. `exec` не создаёт process object.
3. `exit(status)` переводит процесс в `ZOMBIE` до `waitpid`/reap.
4. `waitpid` разрешён только для дочерних процессов вызывающего.
5. `waitpid` собирает статус и завершает lifecycle process object.

FD semantics:

1. `read/write` работают только для валидного открытого `fd`.
2. `close(fd)` инвалидирует номер `fd` для последующих операций.
3. Правила наследования `fd` при `spawn/exec` не могут меняться неявно.
4. `FD_CLOEXEC` — свойство **дескриптора в таблице процесса**, а не
   underlying file object.
5. `spawn` копирует descriptor table в дочерний процесс (семантика copy), а не
   разделяет один и тот же массив дескрипторов между parent/child.
6. `exec` применяет `FD_CLOEXEC` перед входом в новый user image.
7. `fcntl(F_SETFD)` после `spawn` влияет только на descriptor table текущего
   процесса и не ретроактивно на sibling/parent descriptor tables.

Path semantics:

1. Текущий `readdir` — pathname-based (MVP ограничение).
2. Цель: handle-based directory iteration (`fd/opendir`-style).
3. Переход к handle-based режиму должен быть отдельным совместимым шагом.

## 6. Минимальная модель объекта процесса (v1 contract)

У Unix-facing процесса должны быть как минимум поля/эквиваленты:

- `pid`, `ppid`
- `process_state`
- `exit_status`
- `address_space`
- `fd_table`
- `cwd`
- `image_info`
- parent/children links

Состояния процесса (контрактная шкала):

- `NEW`
- `READY`
- `RUNNING`
- `WAITING`
- `ZOMBIE`
- `TERMINATED`

## 7. Совместимость без `fork`: стратегия

Отсутствие `fork` в v1 признано явно. Для практической совместимости shell и
userland развиваем расширенный `spawn` (attrs + file actions), чтобы покрывать:

- redirection;
- pipelines;
- env/cwd setup;
- fd inheritance rules.

Целевая сигнатура (эскиз, не ABI-коммит):

```c
int spawn(const char* path,
          const spawn_attrs_t* attrs,
          const spawn_file_actions_t* actions,
          const char* const argv[],
          const char* const envp[],
          pid_t* out_pid);
```

## 8. Что считается регрессией

1. `spawn` начинает менять текущий процесс вместо создания дочернего.
2. `exec` начинает менять `pid`.
3. `waitpid` разрешает ожидание не-дочернего `pid`.
4. `exit` уничтожает процесс до фазы `ZOMBIE`/collect.
5. `close(fd)` оставляет `fd` рабочим.
