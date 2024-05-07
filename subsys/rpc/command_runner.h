/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_SUBSYS_RPC_COMMAND_RUNNER_H_
#define INFUSE_SDK_SUBSYS_RPC_COMMAND_RUNNER_H_

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Execute a command contained in the buffer
 *
 * @param request Command request
 */
void rpc_command_runner(struct net_buf *request);

/**
 * @brief Free the request buffer inside the RPC implementation
 *
 * Free the request buffer inside the RPC implementation instead of
 * relying on the server to free the buffer after the command returns.
 * This is useful for long running commands with @ref INFUSE_IOT_DATA
 * packets.
 *
 * @param request Request message to free
 */
void rpc_command_runner_request_unref(struct net_buf *request);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_RPC_COMMAND_RUNNER_H_ */
