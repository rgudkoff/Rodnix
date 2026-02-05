/**
 * @file idl_demo.c
 * @brief IDL runtime demo (in-kernel)
 */

#include "ipc.h"
#include "scheduler.h"
#include "../../include/common.h"
#include "../../include/console.h"
#include "../core/task.h"

#include "idl_runtime.h"
#include "../../include/idl/example_ipc.h"

static port_t* demo_server_port = NULL;

static void demo_server_thread(void* arg)
{
    (void)arg;
    demo_server_port = port_allocate(PORT_TYPE_CONTROL);
    if (!demo_server_port) {
        kputs("[IDL-DEMO] server: no port\n");
        return;
    }

    for (;;) {
        ipc_message_t msg;
        if (ipc_receive(demo_server_port, &msg, 0) == 0) {
            if (msg.msg_id == DEMO_MSG_PING) {
                demo_ping_request_t* req = (demo_ping_request_t*)msg.data;
                demo_ping_reply_t reply;
                if (req) {
                    reply.status = req->value + 1;
                    if (msg.reply_port) {
                        idl_ipc_reply(msg.reply_port, DEMO_MSG_PING, &reply, sizeof(reply), 0);
                    }
                }
            }
            ipc_message_free(&msg);
        }
    }
}

int demo_ping_impl(const demo_ping_request_t* req, demo_ping_reply_t* out)
{
    if (!req || !out) {
        return -1;
    }
    out->status = req->value + 1;
    return 0;
}

static void demo_client_thread(void* arg)
{
    (void)arg;
    int spins = 0;
    while (!demo_server_port && spins++ < 100000) {
        __asm__ volatile ("pause");
    }
    if (!demo_server_port) {
        kputs("[IDL-DEMO] client: no server port\n");
        return;
    }
    demo_ping_request_t req;
    demo_ping_reply_t rep;
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));
    req.value = 41;
    if (idl_ipc_call(demo_server_port, DEMO_MSG_PING, &req, sizeof(req), &rep, sizeof(rep), 0) == 0) {
        kprintf("[IDL-DEMO] ping ok, status=%u\n", rep.status);
    } else {
        kputs("[IDL-DEMO] ping failed\n");
    }
}

void idl_demo_start(void)
{
    task_t* task = task_get_current();
    if (!task) {
        return;
    }
    thread_t* server = thread_create(task, demo_server_thread, NULL);
    thread_t* client = thread_create(task, demo_client_thread, NULL);
    if (server) {
        scheduler_add_thread(server);
    }
    if (client) {
        scheduler_add_thread(client);
    }
}
