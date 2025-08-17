/**
 * @file
 * @brief Run RPCs on remote devices
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_CLIENT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_CLIENT_H_

#include <zephyr/net_buf.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
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
	/* Allow the next transmission */
	struct k_sem tx_tokens;
	/* Timeout for the response */
	k_timeout_t rsp_timeout;
	/* Callback to run on completion */
	rpc_client_rsp_fn cb;
	/* Arbitrary user data for callback */
	void *user_data;
	/* RPC request ID */
	uint32_t request_id;
	/* RPC command ID */
	uint16_t command_id;
	/* Number of TX tokens to give on DATA_ACK */
	uint16_t tx_tokens_on_ack;
	/* Result of TX queuing */
	int16_t tx_result;
};

/* RPC client context */
struct rpc_client_ctx {
	const struct device *interface;
	union epacket_interface_address address;
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
 * @param address Interface address to communicate with
 */
void rpc_client_init(struct rpc_client_ctx *ctx, const struct device *dev,
		     union epacket_interface_address address);

/**
 * @brief Get the request ID used by the last command
 *
 * @note Used in conjunction with @ref rpc_client_data_queue.
 *
 * @param ctx RPC client context
 *
 * @return uint32_t request ID
 */
static inline uint32_t rpc_client_last_request_id(const struct rpc_client_ctx *ctx)
{
	return ctx->request_id;
}

/**
 * @brief Update the response timeout of an executing command
 *
 * @note This restarts the response timeout with the new value.
 *
 * @param ctx RPC client context
 * @param request_id Request ID from @ref rpc_client_last_request_id
 * @param timeout New response timeout
 *
 * @retval 0 On success
 * @retval -EINVAL If request ID is no longer valid
 */
int rpc_client_update_response_timeout(struct rpc_client_ctx *ctx, uint32_t request_id,
				       k_timeout_t timeout);

/**
 * @brief Queue a command for execution on a remote device
 *
 * @note The header information in @a req_params is populated by this function.
 *
 * @note When used with @ref rpc_client_data_queue, receiving @ref INFUSE_RPC_DATA_ACK
 *       messages will reset @a response_timeout.
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
 * @brief Wait for an @ref INFUSE_RPC_DATA_ACK from the remote device
 *
 * @note At a minimum, the client should wait for the first ACK from the remote device
 *       before sending data via @ref rpc_client_data_queue. This gives the remote time
 *       to configure itself for receiving data.
 *
 * @param ctx RPC client context
 * @param request_id Request ID from @ref rpc_client_last_request_id
 * @param timeout
 *
 * @retval 0 On success
 * @retval -EINVAL If request ID is no longer valid
 * @retval -EAGAIN If waiting for ACK timed out
 */
int rpc_client_ack_wait(struct rpc_client_ctx *ctx, uint32_t request_id, k_timeout_t timeout);

/**
 * @brief Callback to load more data for queueing
 *
 * @param user_data Arbitrary pointer supplied to @ref rpc_client_data_queue
 * @param offset Offset of data requested
 * @param data Pointer to load data into
 * @param data_len Length of data to load
 *
 * @retval 0 on success
 * @retval -errno on error
 */
typedef int (*rpc_client_data_loader)(void *user_data, uint32_t offset, void *data,
				      size_t data_len);

/**
 * @brief Queue data associated with a previously queued command
 *
 * @param ctx RPC client context
 * @param request_id Request ID from @ref rpc_client_last_request_id
 * @param offset Byte offset of data
 * @param data Command data pointer
 * @param data_len Command data length
 *
 * @retval 0 If data pushed to remote device
 * @retval -EINVAL If request ID is no longer valid
 */
int rpc_client_data_queue(struct rpc_client_ctx *ctx, uint32_t request_id, uint32_t offset,
			  const void *data, size_t data_len);

/** State for auto loader control */
struct rpc_client_auto_load_params {
	/* Callback to load more data */
	rpc_client_data_loader loader;
	/* Total length of data to send */
	uint32_t total_len;
	/* Duration to wait for each DATA_ACK */
	k_timeout_t ack_wait;
	/* Specified DATA_ACK period */
	uint8_t ack_period;
	/* Maximum number of pending DATA_ACK packets */
	uint8_t pipelining;
	/* User data pointer for @a loader */
	void *user_data;
};

/**
 * @brief Queue data associated with a previously queued command, loaded via callback
 *
 * @param ctx RPC client context
 * @param request_id Request ID from @ref rpc_client_last_request_id
 * @param offset Byte offset of data
 * @param buffer Buffer for loading data
 * @param buffer_len Buffer data length
 * @param loader_params Context for loading data
 *
 * @retval 0 If data pushed to remote device
 * @retval -EINVAL If request ID is no longer valid
 */
int rpc_client_data_queue_auto_load(struct rpc_client_ctx *ctx, uint32_t request_id,
				    uint32_t offset, void *buffer, size_t buffer_len,
				    struct rpc_client_auto_load_params *loader_params);

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
 * @retval -ETIMEDOUT On response timeout
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
