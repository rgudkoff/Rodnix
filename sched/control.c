#include "internal.h"
#include "../kernel/core/giant.h"
#include "../kernel/arch/percpu.h"
#include "../kernel/core/interrupts.h"
#include "../trace/tracev2.h"
#include "../trace/bootlog.h"
#include "../include/debug.h"
#include "../include/console.h"
#include "../../include/error.h"

static void scheduler_exit_wake_joiner(thread_t* exiting)
{
    if (!exiting || !exiting->joiner) {
        return;
    }

    thread_t* joiner = exiting->joiner;
    exiting->joiner = NULL;
    if (thread_is_dead(joiner)) {
        return;
    }

    /* Keep wake semantics deterministic for shell/run and POSIX exit paths. */
    joiner->priority = 220;
    joiner->base_priority = 220;
    joiner->dyn_priority = 220;
    joiner->inherited_priority = 220;
    joiner->has_inherited = 0;

    /* Пробуждение адресуется ожиданию, а не потоку: снятие с очереди и
     * доставка — одна операция под её замком. Прямой wake потоку, сидящему
     * в waitq, терялся — TH_WAIT снят, а из очереди никто не забрал, и
     * поток засыпал обратно. */
    waitq_wake_thread(&scheduler_join_waitq, joiner);
}

/*
 * Запустить поток и дождаться его завершения.
 *
 * Порядок жёсткий: сначала объявить ожидание (waitq_enqueue ставит TH_WAIT
 * под замком очереди), и только потом отдать поток планировщику. Его exit
 * тогда не может прийти раньше, чем мы окажемся в очереди, — окна для
 * потерянного пробуждения нет по построению.
 *
 * Это замена сырому scheduler_block() в shell: под протоколом арбитра
 * блокировка без объявленного ожидания не блокирует вовсе (и не должна) —
 * спать можно только на объявленном событии.
 */
int scheduler_thread_start_join(thread_t* thread)
{
    thread_t* self = thread_get_current();
    if (!thread || !self) {
        return RDNX_E_INVALID;
    }
    thread->joiner = self;
    (void)waitq_enqueue(&scheduler_join_waitq, self);
    scheduler_add_thread(thread);
    return waitq_wait_until(&scheduler_join_waitq, 0);
}

static void scheduler_yield_internal(bool irq_context)
{
    if (!scheduler_running) {
        return;
    }
    resched_pending = true;

    if (irq_context || !thread_get_current()) {
        return;
    }

    /*
     * Holding a spinlock means preemption is off and switching is not
     * allowed; leave the request for the next tick, as before.
     */
    if (percpu_preempt_blocked()) {
        return;
    }

    /*
     * Otherwise, yield for real, and give up the kernel-wide lock while
     * doing it.
     *
     * This kernel waits by polling: pipes, waitpid, the tty, the sockets and
     * the reaper all spin on scheduler_yield() rather than sleeping on a
     * wait queue. Seventeen loops, and every one of them would hold Giant
     * across a wait for work that only another thread can do. Dropping it
     * here covers all of them, instead of depending on seventeen call sites
     * to remember -- and one that forgot would wedge the kernel outright.
     *
     * It also makes the name true. This used to set a flag and return, so a
     * caller "yielding" kept running until the next timer tick.
     */
    uint32_t giant_depth = giant_drop();
    interrupt_trigger_resched();
    giant_pickup(giant_depth);
}

void scheduler_yield(void)
{
    scheduler_yield_internal(false);
}

/*
 * Объявить готовность заснуть.
 *
 * Сам сон здесь не происходит и произойти не может: заснуть — значит уйти
 * с процессора, а это делает только switch по прерыванию. Здесь лишь две
 * вещи: быстрая проверка «не разбудили ли уже» (чтобы не дёргать resched
 * впустую) и запрос переключения.
 *
 * TH_RUN этот путь не трогает. Бит снимает единственный арбитр — уходящий
 * процессор в scheduler_switch_from_irq(), под замком потока, когда контекст
 * уже сохранён. Ровно так у XNU: thread_block не снимает TH_RUN, его снимает
 * thread_dispatch после переключения. Снятие здесь и было источником двух
 * билетов на исполнение: пробуждение, пришедшее между этим местом и
 * переключением, заставало TH_RUN снятым и клало поток в очередь, а потом
 * его клал туда же и уходящий процессор.
 */
void scheduler_block(void)
{
    thread_t* cur = thread_get_current();
    if (!cur || in_scheduler) {
        return;
    }

    /* Заснуть с ненулевым счётчиком преемпции — значит оставить его
     * ненулевым чужому потоку: счётчик процессорный, и окно, открытое
     * здесь, закроется неизвестно когда и запишется не на того. Тот же
     * класс, что и спящий ext2-лок. Громко, потому что молча это
     * выглядит как десять миллисекунд запрета преемпции из ниоткуда. */
    if (percpu_preempt_blocked()) {
        static int preempt_block_warns = 0;
        if (preempt_block_warns < 8) {
            preempt_block_warns++;
            kprintf("[SCHED] block with preemption off (count=%u) tid=%llu\n",
                    (unsigned)percpu_self()->preempt_count,
                    (unsigned long long)cur->thread_id);
        }
    }

    uint64_t f = spinlock_lock_irqsave(&cur->sched_lock);
    uint32_t s = cur->state;
    if ((s & TH_WAIT) == 0u || (s & TH_DEAD) != 0u) {
        /* Пробуждение обогнало засыпание: вызывающий перепроверит своё
         * условие и либо пойдёт дальше, либо объявит ожидание заново. */
        spinlock_unlock_irqrestore(&cur->sched_lock, f);
        return;
    }
    /* Фиксация точки сна. До этой строки поток с TH_WAIT — всё ещё
     * бегущий поток (арбитр вернёт его в очередь); после — кандидат на
     * настоящий сон при ближайшем переключении. */
    cur->state = s | TH_BLOCK;
    spinlock_unlock_irqrestore(&cur->sched_lock, f);

    tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_BLOCK, cur->thread_id, s);
    cur->last_sleep_tick = sched_ticks;
    resched_pending = true;
}

/*
 * Разбудить поток.
 *
 * Весь переход — под замком потока, и решение «класть ли в очередь
 * готовности» принимается там же. Правило единственного арбитра, как у XNU:
 *
 *   - TH_RUN снят: поток полностью ушёл с процессора. Ставим TH_RUN, кладём
 *     в очередь. Билет на исполнение выдаём мы, и он один.
 *   - TH_RUN стоит: поток исполняется или уже в очереди. Снимаем только
 *     ожидание — и всё. Класть его будет тот, кто снимет TH_RUN, то есть
 *     уходящий процессор в switch, который под этим же замком увидит, что
 *     ожидания больше нет, и вернёт поток в очередь сам.
 *
 * Раньше в очередь клали оба — waker и уходящий процессор, каждый по своей
 * проверке «не в очереди» вне общего замка. Поток получал два билета, и два
 * процессора исполняли один стек.
 *
 * Ядро перехода вынесено в scheduler_wake_locked(): waitq будит потоки, не
 * отпуская своего замка между снятием с очереди ожидания и доставкой
 * пробуждения, и замок потока к этому моменту уже держит.
 */
void scheduler_wake_locked(thread_t* thread)
{
    uint32_t old = thread->state;
    if (old & TH_DEAD) {
        return;
    }
    thread->state = (old | TH_RUN) & ~(TH_WAIT | TH_BLOCK);

    if ((old & TH_RUN) != 0u) {
        /* На процессоре или в очереди: ожидание снято, добавить нечего. */
        return;
    }

    if (old & TH_WAIT) {
        if (thread->sched_class == SCHED_CLASS_TIMESHARE) {
            uint64_t sleep_ticks = sched_ticks - thread->last_sleep_tick;
            if (sleep_ticks >= BOOST_THRESHOLD_TICKS) {
                int boost = (int)(sleep_ticks / BOOST_THRESHOLD_TICKS);
                if (boost > BOOST_MAX) {
                    boost = BOOST_MAX;
                }
                thread->dyn_priority =
                    clamp_dyn_priority(thread->dyn_priority + boost,
                                       thread->base_priority);
            }
        }
        if (stats.blocked_tasks > 0) {
            stats.blocked_tasks--;
        }
    }

    /* Под замком: пока TH_RUN, поставленный нами, виден остальным, поток
     * уже в очереди. Порядок замков sched_lock -> rq_lock. */
    ready_enqueue(thread);
}

void scheduler_wake(thread_t* thread)
{
    if (!thread) {
        return;
    }

    uint64_t f = spinlock_lock_irqsave(&thread->sched_lock);
    scheduler_wake_locked(thread);
    spinlock_unlock_irqrestore(&thread->sched_lock, f);
    resched_pending = true;
}

void scheduler_exit_current(void)
{
    thread_t* cur = thread_get_current();
    if (!cur) {
        return;
    }

    /*
     * A thread that dies inside a system call still holds the kernel-wide
     * lock it took on entry, and will never reach the release at the other
     * end -- exit() does not return. Give it back here, where every path to
     * a dead thread passes, rather than at each of them.
     */
    (void)giant_drop();

    scheduler_exit_wake_joiner(cur);
    scheduler_thread_set_state(cur, THREAD_STATE_DEAD, "scheduler_exit_current");
    tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_EXIT,
                 cur->thread_id,
                 cur->task ? cur->task->task_id : 0);
    /*
     * Zombie the task only when this is the last live thread.
     * For multi-threaded processes, other threads continue running.
     */
    if (cur->task && cur->task->state != TASK_STATE_DEAD) {
        uint32_t live = 0;
        thread_t* t;
        TAILQ_FOREACH(t, &cur->task->threads, task_link) {
            if (t != cur && !thread_is_dead(t)) {
                live++;
            }
        }
        if (live == 0) {
            scheduler_task_set_state(cur->task, TASK_STATE_ZOMBIE, "scheduler_exit_current");
        }
    }
    resched_pending = true;
    interrupt_trigger_resched();
    for (;;) {
        cpu_idle();
    }
}

void scheduler_sleep(uint64_t milliseconds)
{
    if (!thread_get_current()) {
        return;
    }
    if (milliseconds == 0) {
        scheduler_yield();
        return;
    }
    (void)waitq_wait(&scheduler_sleep_waitq, milliseconds);
}

void scheduler_set_priority(thread_t* thread, uint8_t priority)
{
    if (!thread) {
        return;
    }

    thread_set_priority(thread, priority);
    thread->base_priority = priority;
    thread->dyn_priority = priority;
    if (thread->has_inherited) {
        if (thread->inherited_priority < thread->dyn_priority) {
            thread->inherited_priority = thread->dyn_priority;
        }
    }

    /* TODO: Re-insert thread into appropriate queue based on new priority */
}

void scheduler_inherit_priority(thread_t* target, const thread_t* donor)
{
    if (!target || !donor) {
        return;
    }
    int donor_prio = thread_effective_priority(donor);
    if (target->inherit_depth < 8) {
        target->inherit_stack[target->inherit_depth++] = target->inherited_priority;
    } else {
        target->has_inherit_overflow = 1;
        kprintf("[SCHED] priority inheritance overflow on thread %llu\n",
                (unsigned long long)target->thread_id);
    }
    if (!target->has_inherited || target->inherited_priority < donor_prio) {
        target->inherited_priority = donor_prio;
        target->has_inherited = 1;
    }
}

void scheduler_clear_inherit(thread_t* target)
{
    if (!target) {
        return;
    }
    if (target->has_inherit_overflow) {
        /* Stack was overflowed — restore to base priority conservatively */
        target->inherited_priority = target->base_priority;
        target->inherit_depth = 0;
        target->has_inherit_overflow = 0;
    } else if (target->inherit_depth > 0) {
        target->inherited_priority = target->inherit_stack[--target->inherit_depth];
    } else {
        target->inherited_priority = target->dyn_priority;
    }
    target->has_inherited = 0;
}

void scheduler_set_bucket(thread_t* thread, sched_bucket_t bucket)
{
    if (!thread) {
        return;
    }
    if ((int)bucket >= (int)SCHED_BUCKET_COUNT) {
        return;
    }
    thread->sched_bucket = (uint8_t)bucket;
    thread->sched_bucket_explicit = 1;
}
