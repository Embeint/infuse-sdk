/**
 * @file
 * @brief Run RPCs on remote devices
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_CLIENT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_CLIENT_H_

#include <zephyr/net/buf.h>

#include <infuse/epacket/interface.h>
#include <infuse/rpc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup rpc_client_apis RPC client APIs
 * @{
 */

/**
 * @brief Callback run when the response arrives
 *
 * @param buf Response packet if not NULL, NULL on timeout
 * @param user_data User data provided to @ref rpc_client_command_queue
 */
typedef void (*rpc_client_rsp_fn)(const struct net_buf *buf, void *user_data);

/* Context associated with a single in-flight command */
struct rpc_client_cmd_ctx {
	/* Command timeout timer */
	struct k_timer timeout;
	/* Callback to run on completion */
	rpc_client_rsp_fn cb;
	/* Arbitrary user data for callback */
	void *user_data;
	/* RPC request ID */
	uint32_t request_id;
	/* RPC command ID */
	uint16_t command_id;
};

/* RPC client context */
struct rpc_client_ctx {
	const struct device *interface;
	struct epacket_interface_cb interface_cb;
	struct rpc_client_cmd_ctx cmd_ctx[CONFIG_INFUSE_RPC_CLIENT_MAX_IN_FLIGHT];
	struct k_sem cmd_ctx_sem;
	uint32_t request_id;
};

/**
 * @brief Initialise RPC client object for use
 *
 * @param ctx RPC client context
 * @param dev ePacket interface to send commands on
 */
void rpc_client_init(struct rpc_client_ctx *ctx, const struct device *dev);

/**
 * @brief Queue a command for execution on a remote device
 *
 * @note The header information in @a req_params is populated by this function.
 *
 * @param ctx RPC client context
 * @param cmd Command ID
 * @param req_params Command request parameters
 * @param req_params_len Length of command request
 * @param cb Callback to run when associated response is received
 * @param user_data Arbitrary user data to supply to callback
 * @param ctx_timeout Maximum duration to wait for a command context to become available
 * @param response_timeout Maximum duration to wait for a response to be received from remote device
 *
 * @retval 0 If command pushed to remote device
 * @retval -EAGAIN If command context claim timed out
 * @retval -EINVAL If request params, callback or response timeout are invalid values
 */
int rpc_client_command_queue(struct rpc_client_ctx *ctx, enum rpc_builtin_id cmd, void *req_params,
			     size_t req_params_len, rpc_client_rsp_fn cb, void *user_data,
			     k_timeout_t ctx_timeout, k_timeout_t response_timeout);

/**
 * @brief Queue a command for execution on a remote device and wait for the response
 *
 * @param ctx RPC client context
 * @param cmd Command ID
 * @param req_params Command request parameters
 * @param req_params_len Length of command request
 * @param ctx_timeout Maximum duration to wait for a command context to become available
 * @param response_timeout Maximum duration to wait for a response to be received from remote device
 * @param rsp Output store for response buffer
 *
 * @retval 0 On success
 * @retval -errno Error code from @ref rpc_client_command_queue on error
 */
int rpc_client_command_sync(struct rpc_client_ctx *ctx, enum rpc_builtin_id cmd, void *req_params,
			    size_t req_params_len, k_timeout_t ctx_timeout,
			    k_timeout_t response_timeout, struct net_buf **rsp);

/**
 * @brief Cleanup a RPC client object
 *
 * @param ctx RPC client context
 */
void rpc_client_cleanup(struct rpc_client_ctx *ctx);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_CLIENT_H_ */
