/**
 * @file idl_runtime.h
 * @brief Minimal IDL IPC runtime helpers
 */

#ifndef _RODNIX_IDL_RUNTIME_H
#define _RODNIX_IDL_RUNTIME_H

#include "ipc.h"
#include <stdint.h>

/**
 * Perform an RPC-style IPC call.
 * @param port Destination port
 * @param msg_id Message id
 * @param req Request payload (may be NULL if size == 0)
 * @param req_size Request payload size
 * @param reply Reply buffer (may be NULL if size == 0)
 * @param reply_size Reply buffer size
 * @param timeout Timeout in ms (0 = infinite)
 * @return 0 on success, negative value on error
 */
int idl_ipc_call(port_t* port,
                 uint32_t msg_id,
                 const void* req,
                 uint32_t req_size,
                 void* reply,
                 uint32_t reply_size,
                 uint64_t timeout);

/**
 * Send an RPC-style reply.
 * @param reply_port Port to send reply to
 * @param msg_id Message id
 * @param reply Reply payload (may be NULL if size == 0)
 * @param reply_size Reply payload size
 * @param timeout Timeout in ms (0 = infinite)
 * @return 0 on success, negative value on error
 */
int idl_ipc_reply(port_t* reply_port,
                  uint32_t msg_id,
                  const void* reply,
                  uint32_t reply_size,
                  uint64_t timeout);

#endif /* _RODNIX_IDL_RUNTIME_H */
