/**
 * @file bootstrap.c
 * @brief Kernel-mode bootstrap server placeholder
 */

#include "bootstrap.h"
#include "ipc.h"
#include "task.h"
#include "scheduler.h"
#include "../../include/console.h"
#include <stdint.h>

static void bootstrap_thread(void* arg)
{
    (void)arg;
    port_t* port = ipc_get_bootstrap_port();
    if (!port) {
        kputs("[BOOTSTRAP] No bootstrap port\n");
        return;
    }

    for (;;) {
        ipc_message_t msg;
        if (ipc_receive(port, &msg, 0) == 0) {
            if (msg.reply_port) {
                uint32_t status = 0;
                ipc_message_t reply;
                reply.msg_id = msg.msg_id;
                reply.msg_size = sizeof(status);
                reply.port_count = 0;
                reply.data = (uint8_t*)&status;
                reply.reply_port = NULL;
                ipc_send(msg.reply_port, &reply, 0);
            }
            ipc_message_free(&msg);
        }
    }
}

int bootstrap_init(void)
{
    return 0;
}

void bootstrap_start(void)
{
    task_t* kernel_task = task_get_current();
    if (!kernel_task) {
        return;
    }
    thread_t* thread = thread_create(kernel_task, bootstrap_thread, NULL);
    if (thread) {
        scheduler_add_thread(thread);
    }
}
