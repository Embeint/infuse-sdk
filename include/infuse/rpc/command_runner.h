/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_RPC_COMMAND_RUNNER_H_
#define INFUSE_SDK_SUBSYS_RPC_COMMAND_RUNNER_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <infuse/epacket/packet.h>

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
 * This is useful for long running commands with @ref INFUSE_RPC_DATA
 * packets.
 *
 * @param request Request message to free
 */
void rpc_command_runner_request_unref(struct net_buf *request);

/**
 * @brief Send the response buffer before returning from the RPC implementation
 *
 * Send the response buffer before the RPC implementation returns. This allows
 * RPCs with long post-processing steps to signal the result early, allowing the
 * command initiator to move onto future work while this device finishes up.
 *
 * @note If used, the RPC implementation must return NULL.
 *
 * @param interface Interface to send response on
 * @param address Address to send response to
 * @param auth Authentication level of response
 * @param request_id Request ID associated with the response
 * @param command_id Command ID associated with the response
 * @param response Response message to send
 */
void rpc_command_runner_early_response(const struct device *interface,
				       union epacket_interface_address address,
				       enum epacket_auth auth, uint32_t request_id,
				       uint16_t command_id, struct net_buf *response);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_RPC_COMMAND_RUNNER_H_ */
