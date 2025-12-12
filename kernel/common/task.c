/**
 * @file task.c
 * @brief Task and thread implementation (minimal)
 */

#include "../core/task.h"
#include <stddef.h>

static task_t* current_task = NULL;
static thread_t* current_thread = NULL;

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

/* TODO: Implement other task/thread functions */

