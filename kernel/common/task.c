/**
 * @file task.c
 * @brief Task and thread implementation (minimal)
 */

#include "../core/task.h"
#include "../vm/vm_map.h"
#include "heap.h"
#include "../core/cpu.h"
#include "../arch/x86_64/interrupt_frame.h"
#include "../core/interrupts.h"
#include "../fs/vfs.h"
#include "../unix/unix_layer.h"
#include "../../include/error.h"
#include <stddef.h>
#include <stdint.h>

#define KERNEL_STACK_SIZE (32 * 1024)
#define STACK_POISON_BYTE 0xCC

static task_t* current_task = NULL;
static thread_t* current_thread = NULL;
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
RB_GENERATE_STATIC(task_id_index, task, task_id_link, task_id_cmp);
static struct task_id_index all_tasks_by_id = RB_INITIALIZER(&all_tasks_by_id);

static inline irql_t task_registry_lock(void)
{
    return set_irql(IRQL_HIGH);
}

static inline void task_registry_unlock(irql_t old)
{
    (void)set_irql(old);
}

#define STACK_CACHE_SIZE 32
static void* stack_cache[STACK_CACHE_SIZE] = {0};
static uint32_t stack_cache_count = 0;
static uint64_t stack_cache_hits = 0;
static uint64_t stack_cache_misses = 0;
static uint64_t stack_cache_retired = 0;
static uint64_t stack_cache_poison_failures = 0;

static inline irql_t task_stack_cache_lock(void)
{
    return set_irql(IRQL_HIGH);
}

static inline void task_stack_cache_unlock(irql_t old)
{
    (void)set_irql(old);
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
    thread_t* self = current_thread;
    interrupts_enable();
    if (!self || !self->entry) {
        for (;;) {
            cpu_idle();
        }
    }

    self->state = THREAD_STATE_RUNNING;
    self->entry(self->arg);

    /* Thread finished */
    self->state = THREAD_STATE_DEAD;
    for (;;) {
        cpu_idle();
    }
}

void* task_kernel_stack_acquire(void)
{
    for (;;) {
        void* stack = NULL;
        irql_t old = task_stack_cache_lock();
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
            irql_t old2 = task_stack_cache_lock();
            stack_cache_poison_failures++;
            task_stack_cache_unlock(old2);
            kfree(stack);
            continue;
        }
        return stack;
    }
    irql_t old = task_stack_cache_lock();
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
    irql_t old = task_stack_cache_lock();
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
    irql_t old = task_stack_cache_lock();
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
    return current_task;
}

void task_set_current(task_t* task)
{
    current_task = task;
}

thread_t* thread_get_current(void)
{
    return current_thread;
}

void thread_set_current(thread_t* thread)
{
    current_thread = thread;
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

    irql_t old = task_registry_lock();
    task->task_id = next_task_id++;
    task->parent_task_id = 0;
    task->address_space = NULL;
    task->vm_map = NULL;
    task->vm_brk_base = 0;
    task->vm_brk_end = 0;
    task->vm_mmap_base = 0;
    task->vm_mmap_hint = 0;
    task->state = TASK_STATE_NEW;
    task->uid = 0;
    task->gid = 0;
    task->euid = 0;
    task->egid = 0;
    for (uint32_t i = 0; i < TASK_MAX_FD; i++) {
        task->fd_table[i] = NULL;
        task->fd_flags[i] = 0;
        task->fd_kind[i] = 0;
    }
    task->cwd[0] = '/';
    task->cwd[1] = '\0';
    task->exit_code = 0;
    task->exited = 0;
    task->waited = 0;
    task->main_thread = NULL;
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
    return task;
}

void task_destroy(task_t* task)
{
    if (!task) {
        return;
    }
    irql_t old = task_registry_lock();
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
    for (uint32_t i = 0; i < TASK_MAX_FD; i++) {
        if (task->fd_table[i]) {
            unix_fd_release(task, (int)i);
        }
    }
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
    irql_t old = task_registry_lock();
    task_t* found = RB_FIND(task_id_index, &all_tasks_by_id, &key);
    task_registry_unlock(old);
    return found;
}

void task_set_ids(task_t* task, uint32_t uid, uint32_t gid, uint32_t euid, uint32_t egid)
{
    if (!task) {
        return;
    }
    task->uid = uid;
    task->gid = gid;
    task->euid = euid;
    task->egid = egid;
}

uint32_t task_get_euid(const task_t* task)
{
    return task ? task->euid : 0;
}

uint32_t task_get_egid(const task_t* task)
{
    return task ? task->egid : 0;
}

uint32_t task_get_thread_count(const task_t* task)
{
    return task ? task->thread_count : 0;
}

int task_fd_alloc(task_t* task, void* handle)
{
    if (!task || !handle) {
        return RDNX_E_INVALID;
    }
    for (int i = 0; i < TASK_MAX_FD; i++) {
        if (!task->fd_table[i]) {
            task->fd_table[i] = handle;
            task->fd_flags[i] = 0;
            task->fd_kind[i] = 0;
            return i;
        }
    }
    return RDNX_E_BUSY;
}

void* task_fd_get(task_t* task, int fd)
{
    if (!task || fd < 0 || fd >= TASK_MAX_FD) {
        return NULL;
    }
    return task->fd_table[fd];
}

int task_fd_close(task_t* task, int fd)
{
    if (!task || fd < 0 || fd >= TASK_MAX_FD) {
        return RDNX_E_INVALID;
    }
    if (!task->fd_table[fd]) {
        return RDNX_E_INVALID;
    }
    task->fd_table[fd] = NULL;
    task->fd_flags[fd] = 0;
    task->fd_kind[fd] = 0;
    return RDNX_OK;
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
    task->thread_count++;
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
    task->thread_count++;
    if (!task->main_thread) {
        task->main_thread = thread;
    }

    return thread;
}

void thread_destroy(thread_t* thread)
{
    if (!thread) {
        return;
    }
    if (thread->task && thread->task->thread_count > 0) {
        thread->task->thread_count--;
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
