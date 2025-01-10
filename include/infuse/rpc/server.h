/**
 * @file
 * @brief Remote Procedure Call server
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_SERVER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_SERVER_H_

#include <zephyr/net/buf.h>

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
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_SERVER_H_ */
