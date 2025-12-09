#include "../include/scheduler.h"
#include "../include/console.h"
#include "../include/common.h"

static process_t* process_list = NULL;
static process_t* current_process = NULL;
/* next_pid will be used when process_create is fully implemented */
static uint32_t next_pid __attribute__((unused)) = 1;

/* Stub implementation - to be fully implemented */
int scheduler_init(void)
{
    kputs("[SCHEDULER] Scheduler initialized (stub)\n");
    return 0;
}

process_t* process_create(uint32_t entry_point, uint32_t stack_size, uint32_t priority)
{
    (void)entry_point;
    (void)stack_size;
    (void)priority;
    /* TODO: Implement */
    return NULL;
}

int process_destroy(uint32_t pid)
{
    (void)pid;
    /* TODO: Implement */
    return -1;
}

void schedule(void)
{
    /* TODO: Implement context switching */
}

void process_block(process_t* proc)
{
    (void)proc;
    /* TODO: Implement */
}

void process_unblock(process_t* proc)
{
    (void)proc;
    /* TODO: Implement */
}

process_t* get_current_process(void)
{
    return current_process;
}

process_t* process_find(uint32_t pid)
{
    process_t* proc = process_list;
    while (proc)
    {
        if (proc->pid == pid)
            return proc;
        proc = proc->next;
    }
    return NULL;
}

int process_set_priority(uint32_t pid, uint32_t priority)
{
    (void)pid;
    (void)priority;
    /* TODO: Implement */
    return -1;
}

