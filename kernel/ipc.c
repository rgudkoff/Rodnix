#include "../include/ipc.h"
#include "../include/console.h"
#include "../include/common.h"

/* Stub implementation - to be fully implemented */
int ipc_init(void)
{
    kputs("[IPC] IPC system initialized (stub)\n");
    return 0;
}

int ipc_send(uint32_t to_pid, ipc_message_t* msg)
{
    (void)to_pid;
    (void)msg;
    /* TODO: Implement */
    return -1;
}

int ipc_recv(uint32_t from_pid, ipc_message_t* msg, uint32_t timeout_ms)
{
    (void)from_pid;
    (void)msg;
    (void)timeout_ms;
    /* TODO: Implement */
    return -1;
}

int ipc_has_message(uint32_t pid)
{
    (void)pid;
    /* TODO: Implement */
    return 0;
}

