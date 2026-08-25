#include "internal.h"
#include "../trace/tracev2.h"
#include "../trace/bootlog.h"
#include "../mm/pmap.h"
#include "../kernel/arch/x86_64/gdt.h"
#include "../kernel/core/cpu.h"
#include "../include/debug.h"
#include "../include/console.h"

/*
 * Run the incoming thread on its own address space, or on the kernel's if it
 * has none. Which page table root that is, and whether switching to it costs
 * anything, is the pmap's business -- this used to read CR3 and compare
 * physical addresses here, which is a fact about x86 in a file that schedules
 * threads.
 */
static void scheduler_switch_address_space(thread_t* next)
{
    if (!next) {
        return;
    }
    pmap_t target = (next->task && next->task->address_space)
                        ? next->task->address_space
                        : pmap_kernel();
    pmap_activate(target);
}

/*
 * Pick what to run next: whatever the queue offers, or this processor's idle
 * thread when it offers nothing.
 *
 * The idle thread is why the switch path never has to answer "nothing to
 * run". It is per-CPU and never enters the shared queue, so it is always
 * available and never contended.
 */
static thread_t* sched_take_next(void)
{
    thread_t* next = ready_dequeue();
    if (next) {
        return next;
    }
    return percpu_self()->sched_idle;
}

/*
 * Take ownership of a thread's stack.
 *
 * The wait is the whole point of on_cpu. A thread reaches the run queue
 * while the processor that preempted it is still executing on its kernel
 * stack -- everything between the enqueue and the stub's stack switch. This
 * spins over exactly that window, which is a handful of instructions.
 */
static void sched_claim(thread_t* t)
{
    if (!t) {
        return;
    }
    /*
     * Bounded, and fatal if it runs out. The wait is meant to be a handful of
     * instructions; anything longer means a thread is queued with on_cpu set
     * and no processor left to clear it, which is a lost invariant rather
     * than contention. Panicking names it instead of hanging the machine with
     * nothing on the wire -- this is how the claim-ordering deadlock was
     * found in the first place.
     */
    uint64_t spins = 0;
    while (__atomic_load_n(&t->on_cpu, __ATOMIC_ACQUIRE) != 0) {
        __asm__ volatile ("pause");
        if (++spins == 200000000ULL) {
            panicf("scheduler: tid=%llu stuck on_cpu on cpu%u",
                   (unsigned long long)t->thread_id, (unsigned)cpu_get_id());
        }
    }
    __atomic_store_n(&t->on_cpu, 1u, __ATOMIC_RELEASE);
}

interrupt_frame_t* scheduler_switch_from_irq(interrupt_frame_t* frame)
{
    if (!scheduler_running || !frame) {
        return frame;
    }
    if (in_scheduler) {
        return frame;
    }

    /* Someone on this processor is holding a spinlock. Switching now would
     * leave the lock held by a thread that is no longer running, and the
     * next thread to want it would spin until the holder is scheduled again
     * -- which, if it is spinning too, is never. The reschedule is not lost,
     * only deferred: resched_pending stays set and the next tick takes it. */
    if (percpu_preempt_blocked()) {
        resched_pending = true;
        return frame;
    }

    in_scheduler = true;
    static int log_count = 0;
    thread_t* cur = thread_get_current();
    if (bootlog_is_verbose() && log_count < 8) {
        kprintf("[SCHED] irq switch: resched=%d current=%llu state=%d ready=%llu\n",
                resched_pending ? 1 : 0,
                (unsigned long long)(cur ? cur->thread_id : 0),
                (cur ? (int)cur->state : -1),
                (unsigned long long)stats.ready_tasks);
        log_count++;
    }
    TRACE_EVENT("sched: switch_from_irq");
    /* If no reschedule is pending and we already have a current thread, keep running */
    if (cur && !resched_pending) {
        in_scheduler = false;
        return frame;
    }
    resched_pending = false;


    if (!cur) {
        thread_t* first = sched_take_next();
        PANIC_IF(!first, "scheduler: no runnable threads on first switch");
        sched_claim(first);
        thread_set_current(first);
        if (first->task) {
            task_set_current(first->task);
        }
        scheduler_switch_address_space(first);
        scheduler_update_tss(first);
        sched_arch_apply_thread(first);
        /* First thread ever to run on this CPU: no outgoing FPU state to save,
         * give it a clean one. */
        arch_fpu_restore(first);
        stats.running_tasks = 1;
        stats.total_switches++;
        scheduler_thread_set_state(first, THREAD_STATE_RUNNING, "switch_first");
        scheduler_reset_timeslice(first);
        in_scheduler = false;
        return (interrupt_frame_t*)(uintptr_t)first->context.stack_pointer;
    }

    cur->context.stack_pointer = (uint64_t)(uintptr_t)frame;

    thread_t* next = sched_take_next();
    if (!next || next == cur) {
        if (cur && !thread_is_dead(cur)) {
            /* Ничего не переводим: поток и так исполняется. Пометка
             * "RUNNING" здесь снимала бы TH_WAIT — то есть уничтожала бы
             * объявленное ожидание у потока, которого прерывание застало
             * между waitq_assert_wait() и scheduler_block(). */
            in_scheduler = false;
            return frame;
        }
        /*
         * Never resume a DEAD thread.
         * This indicates that no runnable fallback thread exists.
         */
        PANIC_IF(true, "scheduler: no runnable threads after current thread exit");
        in_scheduler = false;
        return frame;
    }


    /*
     * Claim the incoming thread before giving up the outgoing one.
     *
     * The order matters and getting it wrong deadlocks. Releasing `cur` to
     * the run queue first means another processor can pick it up and wait on
     * its on_cpu -- which this processor only clears once it has finished
     * switching. If it is itself waiting here to claim a thread some third
     * processor holds, the wait is circular and nothing moves again. Measured
     * exactly that: cpu1 waiting on a thread cpu2 had queued, cpu2 waiting on
     * a thread cpu1 held.
     *
     * Claiming first breaks the cycle: while this spins, `cur` is still ours
     * and not in any queue, so no other processor can be waiting on it.
     */
    sched_claim(next);

    thread_t* prev = cur;

    /* Арбитр судьбы уходящего потока. Единственное место, где снимается
     * TH_RUN, и единственное, где уходящий поток возвращается в очередь, —
     * и то и другое под его замком. Пара к scheduler_wake(): кто застал
     * TH_RUN стоящим, тот только снимает ожидание и в очередь не кладёт;
     * значит, класть — наша обязанность, и делаем это ровно один раз.
     *
     * Контекст prev уже сохранён (frame записан на входе в прерывание), так
     * что «снять TH_RUN» здесь честно означает «поток ушёл с процессора».
     * Остаток окна — стаб ещё исполняется на стеке prev — закрывает on_cpu.
     * У XNU эту роль играет thread_dispatch() на стеке нового потока. */
    {
        uint64_t pf = spinlock_lock_irqsave(&prev->sched_lock);
        uint32_t ps = prev->state;
        if (ps & TH_DEAD) {
            /* Судьба решена не нами: ниже поток уйдёт сборщику. */
        } else if ((ps & (TH_WAIT | TH_BLOCK)) == (TH_WAIT | TH_BLOCK)) {
            /* Ожидание объявлено и точка сна достигнута: поток засыпает.
             * Разбудит его scheduler_wake — увидев снятый TH_RUN, он и
             * положит поток в очередь. TH_WAIT без TH_BLOCK сюда не
             * попадает: такой поток ещё идёт к своей точке сна и может
             * держать спящие замки — он остаётся готовым (ветка ниже),
             * как поток между sleepq_add и sleepq_wait у FreeBSD. */
            prev->state = ps & ~(TH_RUN | TH_BLOCK);
            stats.blocked_tasks++;
        } else if (prev != percpu_self()->sched_idle) {
            /* Всё ещё готов — либо не засыпал, либо пробуждение успело снять
             * ожидание раньше нас. TH_RUN остаётся, билет выдаём мы. */
            if (!ready_thread_is_queued(prev)) {
                ready_enqueue(prev);
            }
        }
        spinlock_unlock_irqrestore(&prev->sched_lock, pf);
    }
    /* Tell the stub whose stack it is leaving. Cleared there, after the
     * switch, which is the only moment the claim can honestly be dropped. */
    percpu_self()->sched_prev_oncpu = &prev->on_cpu;

    thread_set_current(next);
    if (next->task) {
        task_set_current(next->task);
    }
    scheduler_switch_address_space(next);
    scheduler_update_tss(next);
    sched_arch_apply_thread(next);
    /* Save the outgoing thread's FPU/SSE registers and load the incoming
     * thread's. The kernel is built -mno-sse and has not touched these since
     * `prev` last ran in user mode, so what FXSAVE captures here is genuinely
     * prev's state. Without this, a thread preempted mid-SSE resumes with
     * another thread's XMM registers and stores corrupt data. */
    arch_fpu_save(prev);
    arch_fpu_restore(next);
    stats.running_tasks = 1;
    stats.total_switches++;


    if (prev && thread_is_dead(prev)) {
        scheduler_reap_enqueue(prev);
    }
    /* Поток взят из очереди готовности, значит TH_RUN у него уже стоит.
     * Переводить его нечем и незачем: снятие TH_WAIT здесь стёрло бы
     * ожидание, объявленное им самим перед тем, как он туда попал. */
    scheduler_reset_timeslice(next);
    tracev2_emit(TR2_CAT_SCHED, TR2_EV_SCHED_SWITCH,
                 prev ? prev->thread_id : 0, next->thread_id);

    in_scheduler = false;
    if (bootlog_is_verbose() && log_count < 8) {
        kprintf("[SCHED] switch to tid=%llu\n",
                (unsigned long long)next->thread_id);
    }
    return (interrupt_frame_t*)(uintptr_t)next->context.stack_pointer;
}
