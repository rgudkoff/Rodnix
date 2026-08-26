/**
 * @file oom.c
 * @brief Убийство по полосам, когда память кончилась и возврат пуст.
 *
 * Полоса на процесс, как jetsam; убивают с нижней полосы вверх, внутри
 * полосы — по освобождаемому, не по начисленному: процесс, у которого вся
 * память разделяемая, при убийстве не освобождает ничего, и убивать его по
 * начисленному — значит регулярно убивать не того.
 *
 * Обязательные свойства (mm_redesign.md): убийство всегда объяснено —
 * полоса, причина, сколько освободило; перед убийством SIGTERM с окном на
 * сброс проекта; ядро, его задачи и полоса CRITICAL не участвуют.
 */

#include "core/oom.h"
#include "core/task.h"
#include "core/ktime.h"
#include "unix/unix_layer.h"
#include "../mm/vm_reclaim.h"
#include "../kernel/arch/pmm.h"
#include "../include/console.h"
#include "../include/error.h"

#define OOM_MAX_CANDIDATES 64
#define OOM_TERM_WINDOW_MS 200

/* Жертва, которой уже послан SIGTERM, и срок её окна. Одна: нехватка
 * памяти — глобальное состояние, и решать её двумя убийствами сразу
 * бессмысленно, пока первое не подействовало. */
static uint64_t g_oom_victim_id;
static uint64_t g_oom_victim_deadline_ns;

typedef struct {
    uint64_t id;
    uint64_t parent;
    uint8_t band;
} oom_cand_t;

typedef struct {
    oom_cand_t c[OOM_MAX_CANDIDATES];
    uint32_t n;
} oom_scan_t;

static void oom_collect(const task_t* t, void* ctx)
{
    oom_scan_t* sc = (oom_scan_t*)ctx;
    if (sc->n >= OOM_MAX_CANDIDATES) {
        return;
    }
    if (t->state == TASK_STATE_DEAD || t->state == TASK_STATE_ZOMBIE) {
        return;
    }
    /* Ядро и его резерв не участвуют: задачи без родителя — это ядро,
     * простой, жнец и init. CRITICAL не участвует по контракту полосы. */
    if (t->parent_task_id == 0 || t->mem_band == (uint8_t)MEMBAND_CRITICAL) {
        return;
    }
    sc->c[sc->n].id = t->task_id;
    sc->c[sc->n].parent = t->parent_task_id;
    sc->c[sc->n].band = t->mem_band;
    sc->n++;
}

/*
 * Один шаг протокола убийства. Возвращает 1, если что-то сделано и
 * вызывающему есть смысл повторить выделение; 0 — сделать больше нечего
 * (в том числе когда лучшая жертва — сам вызывающий: тогда честный ответ
 * на его выделение — отказ, а не самоубийство изнутри отказа страницы).
 */
int oom_kill_step(void)
{
    uint64_t now = ktime_ns();

    /* Окно предыдущей жертвы ещё идёт — или уже вышло. */
    if (g_oom_victim_id != 0) {
        task_t* v = task_find_by_id(g_oom_victim_id);
        if (!v || v->state == TASK_STATE_DEAD || v->state == TASK_STATE_ZOMBIE) {
            g_oom_victim_id = 0;  /* умерла сама: окно сработало */
            return 1;
        }
        if (now < g_oom_victim_deadline_ns) {
            return 1;             /* окно на сброс ещё не вышло: подождать */
        }
        kprintf("[OOM] window expired: task=%llu killed (band=%u)\n",
                (unsigned long long)g_oom_victim_id, (unsigned)v->mem_band);
        {
            extern void task_debug_dump_task(uint64_t);
            task_debug_dump_task(g_oom_victim_id);
        }
        (void)unix_proc_oom_signal(g_oom_victim_id, 1);
        g_oom_victim_id = 0;
        return 1;
    }

    oom_scan_t sc = { .n = 0 };
    task_for_each(oom_collect, &sc);

    /* Проситель выбывает до учёта, а не после выбора: его замок карты уже
     * держит отказ, из которого нас позвали, и считать его память отсюда —
     * значит брать нерекурсивный kmutex вторично. Жертвой он всё равно не
     * стал бы — самоубийству из-под отказа положен честный E_NOMEM. */
    task_t* self = task_get_current();
    if (self) {
        for (uint32_t i = 0; i < sc.n; i++) {
            if (sc.c[i].id == self->task_id) {
                sc.c[i] = sc.c[sc.n - 1];
                sc.n--;
                break;
            }
        }
    }

    /* Нижняя полоса, внутри — по освобождаемому. Учёт — строго trylock:
     * занятая карта означает «кандидат сам стоит в отказе», и блокироваться
     * на нём — это AB-BA-дедлок двух просителей, считающих карты друг
     * друга. Если ни один учёт не снялся, выбор делается по одной полосе,
     * и об этом говорится вслух. */
    int best = -1;
    int best_unaccounted = -1;
    uint64_t best_rc = 0;
    for (uint8_t band = (uint8_t)MEMBAND_BACKGROUND;
         band < (uint8_t)MEMBAND_CRITICAL && best < 0; band++) {
        for (uint32_t i = 0; i < sc.n; i++) {
            if (sc.c[i].band != band) {
                continue;
            }
            task_t* t = task_find_by_id(sc.c[i].id);
            if (!t) {
                continue;
            }
            if (best_unaccounted < 0) {
                best_unaccounted = (int)i;
            }
            uint64_t ch = 0, rc = 0;
            if (!vm_task_mem_account_try(t, &ch, &rc)) {
                continue;
            }
            if (best < 0 || rc > best_rc) {
                best = (int)i;
                best_rc = rc;
            }
        }
        if (best < 0 && best_unaccounted >= 0) {
            break;
        }
    }
    if (best < 0 && best_unaccounted >= 0) {
        kprintf("[OOM] accounts unavailable (maps busy): choosing by band alone\n");
        best = best_unaccounted;
        best_rc = 0;
    }
    if (best < 0) {
        return 0;
    }

    kprintf("[OOM] pressure=%d free=%llu: SIGTERM task=%llu band=%u "
            "reclaimable=%llu pages, %ums window\n",
            vm_pressure_level(),
            (unsigned long long)pmm_free_pages_count(),
            (unsigned long long)sc.c[best].id,
            (unsigned)sc.c[best].band,
            (unsigned long long)best_rc,
            (unsigned)OOM_TERM_WINDOW_MS);
    g_oom_victim_id = sc.c[best].id;
    g_oom_victim_deadline_ns = now + (uint64_t)OOM_TERM_WINDOW_MS * 1000000ull;
    (void)unix_proc_oom_signal(g_oom_victim_id, 0);
    return 1;
}
