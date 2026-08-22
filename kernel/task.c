/**
 * @file task.c
 * @brief Task and thread implementation (minimal)
 */

#include "core/task.h"
#include "fabric/spin.h"
#include "../mm/vm_map.h"
#include "../lib/heap.h"
#include "../sched/scheduler.h"
#include "../sched/waitq.h"
#include "core/cpu.h"
#include "arch/interrupt_frame.h"
#include "arch/percpu.h"
#include "core/interrupts.h"
#include "../fs/vfs.h"
#include "../include/console.h"
#include "../include/error.h"
#include <stddef.h>
#include <stdint.h>

#define KERNEL_STACK_SIZE (32 * 1024)
#define STACK_POISON_BYTE 0xCC

/*
 * LOCKING: task registry — protected by IRQL_HIGH (task_registry_lock / task_registry_unlock).
 *   Protects: all_tasks_head, all_tasks_by_id, next_task_id, next_thread_id.
 *   Mechanism: raises IRQL to IRQL_HIGH (disables interrupts on UP), effectively
 *              acting as a spinlock on uniprocessor.
 *   Lock order: task_registry_lock -> (no inner locks; must NOT acquire ipc locks).
 *
 * LOCKING: stack_cache — protected by IRQL_HIGH (task_stack_cache_lock / task_stack_cache_unlock).
 *   Protects: stack_cache[], stack_cache_count, stack_cache_hits, stack_cache_misses.
 *
 * LOCKING: current task/thread — no lock needed.
 *   They live in struct percpu, reached through this CPU's GS base
 *   (kernel/arch/x86_64/percpu.h). A processor only ever addresses its own
 *   slot, so there is nothing for another CPU to race against; cross-CPU
 *   reads, if ever added, are advisory only.
 */
static uint64_t next_task_id = 1;
static uint64_t next_thread_id = 1;
static task_t* all_tasks_head = NULL;
RB_HEAD(task_id_index, task);
static int task_id_cmp(task_t* lhs, task_t* rhs)
{
    if (lhs->task_id < rhs->task_id) {
        return -1;
    }
    if (lhs->task_id > rhs->task_id) {
        return 1;
    }
    return 0;
}
RB_PROTOTYPE_STATIC(task_id_index, task, task_id_link, task_id_cmp);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
RB_GENERATE_STATIC(task_id_index, task, task_id_link, task_id_cmp);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
static struct task_id_index all_tasks_by_id = RB_INITIALIZER(&all_tasks_by_id);

/* Real mutual exclusion: the registry is walked and mutated from every
 * processor, and masking interrupts only ever excluded this one. */
static spinlock_t task_registry_spin;

static inline uint64_t task_registry_lock(void)
{
    return spinlock_lock_irqsave(&task_registry_spin);
}

static inline void task_registry_unlock(uint64_t flags)
{
    spinlock_unlock_irqrestore(&task_registry_spin, flags);
}

#define STACK_CACHE_SIZE 32
static void* stack_cache[STACK_CACHE_SIZE] = {0};
static uint32_t stack_cache_count = 0;
static uint64_t stack_cache_hits = 0;
static uint64_t stack_cache_misses = 0;
static uint64_t stack_cache_retired = 0;
static uint64_t stack_cache_poison_failures = 0;

/* Same reasoning as the registry lock above. */
static spinlock_t task_stack_cache_spin;

static inline uint64_t task_stack_cache_lock(void)
{
    return spinlock_lock_irqsave(&task_stack_cache_spin);
}

static inline void task_stack_cache_unlock(uint64_t flags)
{
    spinlock_unlock_irqrestore(&task_stack_cache_spin, flags);
}

static bool stack_has_poison(const void* stack)
{
    if (!stack) {
        return false;
    }
    uint64_t p = 0;
    for (int i = 0; i < 8; i++) {
        p = (p << 8) | (uint64_t)STACK_POISON_BYTE;
    }
    const uint64_t* lo = (const uint64_t*)stack;
    const uint64_t* hi = (const uint64_t*)((const uint8_t*)stack + KERNEL_STACK_SIZE - sizeof(uint64_t));
    return (*lo == p) && (*hi == p);
}

static void thread_trampoline(void)
{
    thread_t* self = thread_get_current();
    interrupts_enable();
    if (!self || !self->entry) {
        for (;;) {
            cpu_idle();
        }
    }

    self->state = THREAD_STATE_RUNNING;
    self->entry(self->arg);

    /* Thread finished — hand off to scheduler for proper teardown. */
    scheduler_exit_current();
}

void* task_kernel_stack_acquire(void)
{
    for (;;) {
        void* stack = NULL;
        uint64_t old = task_stack_cache_lock();
        if (stack_cache_count > 0) {
            stack = stack_cache[--stack_cache_count];
            stack_cache[stack_cache_count] = NULL;
            stack_cache_hits++;
        }
        task_stack_cache_unlock(old);

        if (!stack) {
            break;
        }
        if (!stack_has_poison(stack)) {
            uint64_t old2 = task_stack_cache_lock();
            stack_cache_poison_failures++;
            task_stack_cache_unlock(old2);
            kfree(stack);
            continue;
        }
        return stack;
    }
    uint64_t old = task_stack_cache_lock();
    stack_cache_misses++;
    task_stack_cache_unlock(old);
    return kmalloc(KERNEL_STACK_SIZE);
}

void task_kernel_stack_retire(void* stack, size_t size)
{
    if (!stack || size != KERNEL_STACK_SIZE) {
        return;
    }
    extern void* memset(void* s, int c, size_t n);
    memset(stack, STACK_POISON_BYTE, KERNEL_STACK_SIZE);
    uint64_t old = task_stack_cache_lock();
    stack_cache_retired++;
    if (stack_cache_count < STACK_CACHE_SIZE) {
        stack_cache[stack_cache_count++] = stack;
        task_stack_cache_unlock(old);
        return;
    }
    task_stack_cache_unlock(old);
    kfree(stack);
}

int task_get_stack_cache_stats(task_stack_cache_stats_t* out_stats)
{
    if (!out_stats) {
        return RDNX_E_INVALID;
    }
    uint64_t old = task_stack_cache_lock();
    out_stats->cache_count = stack_cache_count;
    out_stats->cache_capacity = STACK_CACHE_SIZE;
    out_stats->cache_hits = stack_cache_hits;
    out_stats->cache_misses = stack_cache_misses;
    out_stats->retired = stack_cache_retired;
    out_stats->poison_failures = stack_cache_poison_failures;
    task_stack_cache_unlock(old);
    return RDNX_OK;
}

task_t* task_get_current(void)
{
    return percpu_self()->current_task;
}

void task_set_current(task_t* task)
{
    percpu_self()->current_task = task;
}

thread_t* thread_get_current(void)
{
    return percpu_self()->current_thread;
}

void thread_set_current(thread_t* thread)
{
    percpu_self()->current_thread = thread;
}

void thread_set_priority(thread_t* thread, uint8_t priority)
{
    if (thread) {
        thread->priority = priority;
    }
}

task_t* task_create(void)
{
    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) {
        return NULL;
    }

    uint64_t old = task_registry_lock();
    task->task_id = next_task_id++;
    task->parent_task_id = 0;
    task->address_space = NULL;
    task->vm_map = NULL;
    task->vm_brk_base = 0;
    task->vm_brk_end = 0;
    task->vm_mmap_base = 0;
    task->vm_mmap_hint = 0;
    task->vm_brk_base = 0;
    task->vm_brk_end = 0;
    task->vm_mmap_base = 0;
    task->vm_mmap_hint = 0;
    task->state = TASK_STATE_NEW;
    task->abi = TASK_ABI_NATIVE;
    task->tls_fs_base = 0;
    task->proc = NULL;
    task->main_thread = NULL;
    TAILQ_INIT(&task->threads);
    task->thread_count = 0;
    task->ref_count = 1;
    task->task_id_link.rbe_link[0] = NULL;
    task->task_id_link.rbe_link[1] = NULL;
    task->task_id_link.rbe_link[2] = NULL;
    task->next_all = all_tasks_head;
    all_tasks_head = task;
    (void)RB_INSERT(task_id_index, &all_tasks_by_id, task);
    task_registry_unlock(old);
    task->arch_specific = NULL;

    /* UNIX-персоналия живёт в POSIX-слое; ядро только владеет её временем жизни. */
    if (!proc_attach(task)) {
        task_destroy(task);
        return NULL;
    }
    return task;
}

void task_destroy(task_t* task)
{
    if (!task) {
        return;
    }
    uint64_t old = task_registry_lock();
    if (all_tasks_head == task) {
        all_tasks_head = task->next_all;
    } else {
        for (task_t* it = all_tasks_head; it; it = it->next_all) {
            if (it->next_all == task) {
                it->next_all = task->next_all;
                break;
            }
        }
    }
    (void)RB_REMOVE(task_id_index, &all_tasks_by_id, task);
    task_registry_unlock(old);
    /* Destroy all threads still attached to this task (P0-4).
     * The current thread is handled by the reaper and is not in the list
     * at this point, so iterating the full list is safe. */
    thread_t* thr;
    thread_t* tmp;
    TAILQ_FOREACH_SAFE(thr, &task->threads, task_link, tmp) {
        thread_destroy(thr);
    }
    proc_detach(task);
    vm_task_destroy(task);
    kfree(task);
}

task_t* task_find_by_id(uint64_t task_id)
{
    if (task_id == 0) {
        return NULL;
    }
    task_t key = {0};
    key.task_id = task_id;
    uint64_t old = task_registry_lock();
    task_t* found = RB_FIND(task_id_index, &all_tasks_by_id, &key);
    task_registry_unlock(old);
    return found;
}

void task_set_abi(task_t* task, task_abi_t abi)
{
    if (!task) {
        return;
    }
    task->abi = (uint8_t)abi;
}

task_abi_t task_get_abi(const task_t* task)
{
    if (!task) {
        return TASK_ABI_NATIVE;
    }
    return (task->abi == (uint8_t)TASK_ABI_LINUX) ? TASK_ABI_LINUX : TASK_ABI_NATIVE;
}

uint32_t task_get_thread_count(const task_t* task)
{
    return task ? task->thread_count : 0;
}

thread_t* thread_create(task_t* task, void (*entry)(void*), void* arg)
{
    if (!task || !entry) {
        return NULL;
    }

    thread_t* thread = (thread_t*)kmalloc(sizeof(thread_t));
    if (!thread) {
        return NULL;
    }

    void* stack = task_kernel_stack_acquire();
    if (!stack) {
        kfree(thread);
        return NULL;
    }

    uintptr_t sp = (uintptr_t)stack + KERNEL_STACK_SIZE;
    sp &= ~(uintptr_t)0xF; /* 16-byte align */
    /* Ensure RSP%16==8 at thread_trampoline entry after iretq */
    sp -= 8;
    sp -= sizeof(interrupt_frame_t);
    interrupt_frame_t* frame = (interrupt_frame_t*)sp;
    extern void* memset(void* s, int c, size_t n);
    memset(frame, 0, sizeof(*frame));
    frame->rip = (uint64_t)(uintptr_t)thread_trampoline;
    uint16_t cs = 0;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    frame->cs = cs;      /* use current kernel code segment */
    frame->rflags = 0x202; /* IF=1, reserved bit set */
    frame->int_no = 0;
    frame->err_code = 0;
    frame->rsp = (uint64_t)(uintptr_t)(stack + KERNEL_STACK_SIZE - 8);
    frame->ss = 0x10;

    thread->thread_id = next_thread_id++;
    thread->task = task;
    thread->context.stack_pointer = (uint64_t)(uintptr_t)frame;
    thread->context.program_counter = frame->rip;
    thread->state = THREAD_STATE_NEW;
    thread->sched_class = SCHED_CLASS_TIMESHARE;
    thread->priority = PRIORITY_DEFAULT;
    thread->base_priority = PRIORITY_DEFAULT;
    thread->dyn_priority = PRIORITY_DEFAULT;
    thread->inherited_priority = PRIORITY_DEFAULT;
    thread->has_inherited = 0;
    thread->inherit_depth = 0;
    for (size_t i = 0; i < 4; i++) {
        thread->inherit_stack[i] = PRIORITY_DEFAULT;
    }
    thread->sched_usage = 0;
    thread->last_sleep_tick = 0;
    thread->entry = entry;
    thread->arg = arg;
    thread->stack = stack;
    thread->stack_size = KERNEL_STACK_SIZE;
    thread->task_link.tqe_next = NULL;
    thread->task_link.tqe_prev = NULL;
    thread->sched_link.tqe_next = NULL;
    thread->sched_link.tqe_prev = NULL;
    thread->ready_queued = 0;
    thread->wait_link.tqe_next = NULL;
    thread->wait_link.tqe_prev = NULL;
    thread->wait_timeout_link.tqe_next = NULL;
    thread->wait_timeout_link.tqe_prev = NULL;
    thread->waitq_owner = NULL;
    thread->wait_deadline_tick = 0;
    thread->wait_timeout_armed = 0;
    thread->wait_timed_out = 0;
    thread->joiner = NULL;
    thread->reap_queued = 0;
    thread->reap_after_tick = 0;
    thread->arch_specific = NULL;
    thread->tls_fs_base = 0;
    task->thread_count++;
    TAILQ_INSERT_TAIL(&task->threads, thread, task_link);
    if (!task->main_thread) {
        task->main_thread = thread;
    }

    return thread;
}

thread_t* thread_create_user_clone(task_t* task, const interrupt_frame_t* frame)
{
    if (!task || !frame) {
        return NULL;
    }

    thread_t* thread = (thread_t*)kmalloc(sizeof(thread_t));
    if (!thread) {
        return NULL;
    }

    void* stack = task_kernel_stack_acquire();
    if (!stack) {
        kfree(thread);
        return NULL;
    }

    uintptr_t sp = (uintptr_t)stack + KERNEL_STACK_SIZE;
    sp &= ~(uintptr_t)0xF;
    sp -= 8;
    sp -= sizeof(interrupt_frame_t);
    interrupt_frame_t* child_frame = (interrupt_frame_t*)sp;
    *child_frame = *frame;
    child_frame->rax = 0; /* fork() return in child */

    thread->thread_id = next_thread_id++;
    thread->task = task;
    thread->context.stack_pointer = (uint64_t)(uintptr_t)child_frame;
    thread->context.program_counter = child_frame->rip;
    thread->state = THREAD_STATE_NEW;
    thread->sched_class = SCHED_CLASS_TIMESHARE;
    thread->priority = PRIORITY_DEFAULT;
    thread->base_priority = PRIORITY_DEFAULT;
    thread->dyn_priority = PRIORITY_DEFAULT;
    thread->inherited_priority = PRIORITY_DEFAULT;
    thread->has_inherited = 0;
    thread->inherit_depth = 0;
    for (size_t i = 0; i < 4; i++) {
        thread->inherit_stack[i] = PRIORITY_DEFAULT;
    }
    thread->sched_usage = 0;
    thread->last_sleep_tick = 0;
    thread->entry = NULL;
    thread->arg = NULL;
    thread->stack = stack;
    thread->stack_size = KERNEL_STACK_SIZE;
    thread->task_link.tqe_next = NULL;
    thread->task_link.tqe_prev = NULL;
    thread->sched_link.tqe_next = NULL;
    thread->sched_link.tqe_prev = NULL;
    thread->ready_queued = 0;
    thread->wait_link.tqe_next = NULL;
    thread->wait_link.tqe_prev = NULL;
    thread->wait_timeout_link.tqe_next = NULL;
    thread->wait_timeout_link.tqe_prev = NULL;
    thread->waitq_owner = NULL;
    thread->wait_deadline_tick = 0;
    thread->wait_timeout_armed = 0;
    thread->wait_timed_out = 0;
    thread->joiner = NULL;
    thread->reap_queued = 0;
    thread->reap_after_tick = 0;
    thread->arch_specific = NULL;
    thread->tls_fs_base = 0;
    thread->clear_tid_ptr = NULL;
    task->thread_count++;
    TAILQ_INSERT_TAIL(&task->threads, thread, task_link);
    if (!task->main_thread) {
        task->main_thread = thread;
    }

    return thread;
}

thread_t* thread_create_user_thread(task_t* task,
                                    const interrupt_frame_t* frame,
                                    uint64_t child_stack,
                                    uint64_t tls_fs_base)
{
    if (!task || !frame) {
        return NULL;
    }

    thread_t* thread = (thread_t*)kmalloc(sizeof(thread_t));
    if (!thread) {
        return NULL;
    }

    void* stack = task_kernel_stack_acquire();
    if (!stack) {
        kfree(thread);
        return NULL;
    }

    uintptr_t sp = (uintptr_t)stack + KERNEL_STACK_SIZE;
    sp &= ~(uintptr_t)0xF;
    sp -= 8;
    sp -= sizeof(interrupt_frame_t);
    interrupt_frame_t* child_frame = (interrupt_frame_t*)sp;
    *child_frame = *frame;
    child_frame->rax = 0;           /* clone() returns 0 in new thread */
    child_frame->rsp = child_stack; /* new thread's user-space stack */

    thread->thread_id = next_thread_id++;
    thread->task = task;
    thread->context.stack_pointer = (uint64_t)(uintptr_t)child_frame;
    thread->context.program_counter = child_frame->rip;
    thread->state = THREAD_STATE_NEW;
    thread->sched_class = SCHED_CLASS_TIMESHARE;
    thread->priority = PRIORITY_DEFAULT;
    thread->base_priority = PRIORITY_DEFAULT;
    thread->dyn_priority = PRIORITY_DEFAULT;
    thread->inherited_priority = PRIORITY_DEFAULT;
    thread->has_inherited = 0;
    thread->inherit_depth = 0;
    for (size_t i = 0; i < 8; i++) {
        thread->inherit_stack[i] = PRIORITY_DEFAULT;
    }
    thread->has_inherit_overflow = 0;
    thread->sched_usage = 0;
    thread->last_sleep_tick = 0;
    thread->entry = NULL;
    thread->arg = NULL;
    thread->stack = stack;
    thread->stack_size = KERNEL_STACK_SIZE;
    thread->task_link.tqe_next = NULL;
    thread->task_link.tqe_prev = NULL;
    thread->sched_link.tqe_next = NULL;
    thread->sched_link.tqe_prev = NULL;
    thread->ready_queued = 0;
    thread->wait_link.tqe_next = NULL;
    thread->wait_link.tqe_prev = NULL;
    thread->wait_timeout_link.tqe_next = NULL;
    thread->wait_timeout_link.tqe_prev = NULL;
    thread->waitq_owner = NULL;
    thread->wait_deadline_tick = 0;
    thread->wait_timeout_armed = 0;
    thread->wait_timed_out = 0;
    thread->joiner = NULL;
    thread->reap_queued = 0;
    thread->reap_after_tick = 0;
    thread->arch_specific = NULL;
    thread->tls_fs_base = tls_fs_base;
    thread->clear_tid_ptr = NULL;
    thread->sched_bucket = SCHED_BUCKET_DEFAULT;
    thread->sched_bucket_explicit = 0;
    task->thread_count++;
    TAILQ_INSERT_TAIL(&task->threads, thread, task_link);

    return thread;
}

void thread_destroy(thread_t* thread)
{
    if (!thread) {
        return;
    }
    if (thread->task) {
        TAILQ_REMOVE(&thread->task->threads, thread, task_link);
        if (thread->task->thread_count > 0) {
            thread->task->thread_count--;
        }
    }
    if (thread->stack) {
        task_kernel_stack_retire(thread->stack, thread->stack_size);
    }
    kfree(thread);
}

void thread_switch(thread_t* from, thread_t* to)
{
    if (!to || from == to) {
        return;
    }

    thread_set_current(to);
    if (to->task) {
        task_set_current(to->task);
    }

    /* x86_64: TSS.RSP0 must point to the incoming thread's kernel stack top
     * so that ring-3 → ring-0 transitions (int 0x80, syscall) land on the
     * correct stack.  Without this update every thread after the first one
     * would use the FIRST thread's kernel stack, corrupting it. */
    if (to->stack) {
        extern void tss_set_rsp0(uint64_t rsp0);
        tss_set_rsp0((uint64_t)(uintptr_t)to->stack + to->stack_size);
    }

    /* Apply the incoming thread's FS.Base (TLS) to the hardware and update
     * the ISR shadow variable.  sched_arch_apply_thread handles both cases:
     *   - thread->tls_fs_base set (CLONE_SETTLS / inherited fork child)
     *   - Linux ABI tasks where arch_prctl only updates task->tls_fs_base */
    sched_arch_apply_thread(to);

    if (!from) {
        cpu_restore_context(&to->context);
    } else {
        cpu_switch_thread(&from->context, &to->context);
    }
}

void thread_block(thread_t* thread)
{
    if (!thread) {
        return;
    }
    thread->state = THREAD_STATE_BLOCKED;
}

void thread_unblock(thread_t* thread)
{
    if (!thread) {
        return;
    }
    if (thread->state == THREAD_STATE_BLOCKED) {
        thread->state = THREAD_STATE_READY;
    }
}

void task_for_each(task_iter_fn_t fn, void* ctx)
{
    if (!fn) {
        return;
    }
    uint64_t old = task_registry_lock();
    for (task_t* it = all_tasks_head; it; it = it->next_all) {
        fn(it, ctx);
    }
    task_registry_unlock(old);
}
