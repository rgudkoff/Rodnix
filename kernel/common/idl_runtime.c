/**
 * @file idl_runtime.c
 * @brief Minimal IDL IPC runtime helpers
 */

#include "idl_runtime.h"
#include "ipc.h"
#include "../../include/common.h"

int idl_ipc_call(port_t* port,
                 uint32_t msg_id,
                 const void* req,
                 uint32_t req_size,
                 void* reply,
                 uint32_t reply_size,
                 uint64_t timeout)
{
    if (!port) {
        return -1;
    }
    if (req_size > IPC_MSG_MAX_SIZE || reply_size > IPC_MSG_MAX_SIZE) {
        return -1;
    }

    port_t* reply_port = port_allocate(PORT_TYPE_CONTROL);
    if (!reply_port) {
        return -1;
    }

    ipc_message_t send_msg;
    memset(&send_msg, 0, sizeof(send_msg));
    send_msg.msg_id = msg_id;
    send_msg.msg_size = req_size;
    send_msg.port_count = 0;
    send_msg.data = (uint8_t*)req;
    send_msg.reply_port = reply_port;

    ipc_message_t reply_msg;
    memset(&reply_msg, 0, sizeof(reply_msg));

    int ret = ipc_send_receive(port, &send_msg, &reply_msg, timeout);
    if (ret == 0) {
        if (reply && reply_size > 0) {
            if (reply_msg.msg_size < reply_size) {
                ipc_message_free(&reply_msg);
                port_deallocate(reply_port);
                return -1;
            }
            memcpy(reply, reply_msg.data, reply_size);
        }
        ipc_message_free(&reply_msg);
    }

    port_deallocate(reply_port);
    return ret;
}

int idl_ipc_reply(port_t* reply_port,
                  uint32_t msg_id,
                  const void* reply,
                  uint32_t reply_size,
                  uint64_t timeout)
{
    if (!reply_port) {
        return -1;
    }
    if (reply_size > IPC_MSG_MAX_SIZE) {
        return -1;
    }

    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_id = msg_id;
    msg.msg_size = reply_size;
    msg.port_count = 0;
    msg.data = (uint8_t*)reply;
    msg.reply_port = NULL;
    return ipc_send(reply_port, &msg, timeout);
}
