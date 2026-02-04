/**
 * @file task.c
 * @brief Task and thread implementation (minimal)
 */

#include "../core/task.h"
#include "heap.h"
#include "../core/cpu.h"
#include "../core/interrupts.h"
#include <stddef.h>
#include <stdint.h>

#define KERNEL_STACK_SIZE (16 * 1024)

static task_t* current_task = NULL;
static thread_t* current_thread = NULL;
static uint64_t next_task_id = 1;
static uint64_t next_thread_id = 1;

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

    task->task_id = next_task_id++;
    task->address_space = NULL;
    task->state = TASK_STATE_NEW;
    task->ref_count = 1;
    task->arch_specific = NULL;
    return task;
}

void task_destroy(task_t* task)
{
    if (!task) {
        return;
    }
    kfree(task);
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

    void* stack = kmalloc(KERNEL_STACK_SIZE);
    if (!stack) {
        kfree(thread);
        return NULL;
    }

    uintptr_t sp = (uintptr_t)stack + KERNEL_STACK_SIZE;
    sp &= ~(uintptr_t)0xF; /* 16-byte align */
    /* Reserve space for saved regs + return address + padding (64 bytes). */
    sp -= 64;
    uint64_t* frame = (uint64_t*)sp;
    frame[0] = 0; /* r15 */
    frame[1] = 0; /* r14 */
    frame[2] = 0; /* r13 */
    frame[3] = 0; /* r12 */
    frame[4] = 0; /* rbp */
    frame[5] = 0; /* rbx */
    frame[6] = (uint64_t)(uintptr_t)thread_trampoline; /* ret */
    frame[7] = 0; /* padding */

    thread->thread_id = next_thread_id++;
    thread->task = task;
    thread->context.stack_pointer = sp;
    thread->context.program_counter = 0;
    thread->state = THREAD_STATE_NEW;
    thread->priority = PRIORITY_DEFAULT;
    thread->entry = entry;
    thread->arg = arg;
    thread->stack = stack;
    thread->stack_size = KERNEL_STACK_SIZE;
    thread->sched_next = NULL;
    thread->arch_specific = NULL;

    return thread;
}

void thread_destroy(thread_t* thread)
{
    if (!thread) {
        return;
    }
    if (thread->stack) {
        kfree(thread->stack);
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
