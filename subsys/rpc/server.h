/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_SUBSYS_RPC_SERVER_H_
#define INFUSE_SDK_SUBSYS_RPC_SERVER_H_

#include <zephyr/net/buf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RPC_SERVER_MAX_ACK_PERIOD 8

/**
 * @brief Create an @ref INFUSE_RPC_RSP packet buffer for an interface
 *
 * @param interface Interface response will be sent on
 * @param rc Return code of the RPC
 * @param response RPC response struct
 * @param len Size of RPC response struct
 *
 * @return struct net_buf* packet buffer
 */
struct net_buf *rpc_response_simple_if(const struct device *interface, int16_t rc, void *response,
				       size_t len);

/**
 * @brief Create an @ref INFUSE_RPC_RSP packet buffer from a request
 *
 * @param request @ref INFUSE_RPC_REQ packet to respond to
 * @param rc Return code of the RPC
 * @param response RPC response struct
 * @param len Size of RPC response struct
 *
 * @return struct net_buf* packet buffer
 */
struct net_buf *rpc_response_simple_req(struct net_buf *request, int16_t rc, void *response,
					size_t len);

/**
 * @brief Get the size of the variable component of the @ref INFUSE_RPC_REQ packet
 */
#define RPC_REQUEST_VAR_LEN(request, type) (request->len - sizeof(type))

/**
 * @brief Get the size of the variable component of the @ref INFUSE_RPC_DATA packet
 */
#define RPC_DATA_VAR_LEN(data) (data->len - sizeof(struct infuse_rpc_data))

/**
 * @brief Attempt to pull @ref INFUSE_RPC_DATA packet from queue
 *
 * @param request_id RPC request ID
 * @param expected_offset Expected data offset
 * @param timeout Duration to wait for packet
 *
 * @retval buf @ref INFUSE_RPC_DATA packet on success
 * @retval NULL on timeout
 */
struct net_buf *rpc_server_pull_data(uint32_t request_id, uint32_t expected_offset,
				     k_timeout_t timeout);

/**
 * @brief Acknowledge received data packets
 *
 * @param interface ePacket interface
 * @param request_id RPC request ID
 * @param offset Offset of the received data
 * @param ack_period RX acknowledgment period
 */
void rpc_server_ack_data(const struct device *interface, uint32_t request_id, uint32_t offset,
			 uint8_t ack_period);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_RPC_SERVER_H_ */
