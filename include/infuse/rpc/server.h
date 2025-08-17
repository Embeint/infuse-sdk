/**
 * @file
 * @brief Remote Procedure Call server
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_SERVER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_SERVER_H_

#include <zephyr/net_buf.h>

#include <infuse/epacket/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RPC server API
 * @defgroup rpc_server_apis RPC server APIs
 * @{
 */

/** Maximum ACK period supported by the RPC server */
#define RPC_SERVER_MAX_ACK_PERIOD 8

/**
 * @brief Push command ePacket to RPC server
 *
 * @param buf @ref INFUSE_RPC_CMD ePacket buffer
 */
void rpc_server_queue_command(struct net_buf *buf);

/**
 * @brief Push data ePacket to RPC server
 *
 * @param buf @ref INFUSE_RPC_DATA ePacket buffer
 */
void rpc_server_queue_data(struct net_buf *buf);

/**
 * @brief Command handling for user-defined RPCs
 *
 * @param command_id RPC command identifier
 * @param auth Authentication level of @a request
 * @param request RPC request packet
 * @param response Storage for RPC response pointer (NULL when called)
 *
 * @retval 0 Command exists and was successfully run
 * @retval -EACCES Authentication level was not sufficient to run command
 * @retval -ENOTSUP Command implementation does not exist
 */
int infuse_rpc_server_user_command_runner(uint16_t command_id, enum epacket_auth auth,
					  struct net_buf *request, struct net_buf **response);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_SERVER_H_ */
