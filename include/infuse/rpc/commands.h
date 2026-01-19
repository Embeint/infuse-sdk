/**
 * @file
 * @brief RPC command implementation functions
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Functions to call from RPC command implementations only.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_H_

#include <zephyr/net_buf.h>

#include <infuse/epacket/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RPC command implementation API
 * @defgroup rpc_commands_apis commands RPC command implementation API
 * @{
 */

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
 * @param request @ref INFUSE_RPC_CMD packet to respond to
 * @param rc Return code of the RPC
 * @param response RPC response struct
 * @param len Size of RPC response struct
 *
 * @return struct net_buf* packet buffer
 */
struct net_buf *rpc_response_simple_req(struct net_buf *request, int16_t rc, void *response,
					size_t len);

/**
 * @brief Get the size of the variable component of the @ref INFUSE_RPC_CMD packet
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
 * @param err Error code when function returned NULL
 * @param timeout Duration to wait for packet
 *
 * @retval buf @ref INFUSE_RPC_DATA packet on success
 * @retval NULL on error
 */
struct net_buf *rpc_server_pull_data(uint32_t request_id, uint32_t expected_offset, int *err,
				     k_timeout_t timeout);

/**
 * @brief Attempt to pull unaligned @ref INFUSE_RPC_DATA packet from queue
 *
 * Unlike @ref rpc_server_pull_data, the offsets are not expected to be aligned to word
 * boundaries.
 *
 * @param request_id RPC request ID
 * @param expected_offset Expected data offset
 * @param err Error code when function returned NULL
 * @param timeout Duration to wait for packet
 *
 * @retval buf @ref INFUSE_RPC_DATA packet on success
 * @retval NULL on error
 */
struct net_buf *rpc_server_pull_data_unaligned(uint32_t request_id, uint32_t expected_offset,
					       int *err, k_timeout_t timeout);

/**
 * @brief Send initial @ref INFUSE_RPC_DATA_ACK to signify we are ready for data
 *
 * @param rx_meta Metadata of the request packet
 * @param request_id RPC request ID
 */
void rpc_server_ack_data_ready(struct epacket_rx_metadata *rx_meta, uint32_t request_id);

/**
 * @brief Acknowledge received data packets
 *
 * @param rx_meta Metadata of the request packet
 * @param request_id RPC request ID
 * @param offset Offset of the received data
 * @param ack_period RX acknowledgment period
 */
void rpc_server_ack_data(struct epacket_rx_metadata *rx_meta, uint32_t request_id, uint32_t offset,
			 uint8_t ack_period);

/**
 * @brief Feed the RPC server watchdog from a RPC implementation
 */
void rpc_server_watchdog_feed(void);

/**
 * @brief Retrieve working memory for an RPC
 *
 * @param size Size of the working memory buffer
 *
 * @return uint8_t* Pointer to working memory
 */
uint8_t *rpc_server_command_working_mem(size_t *size);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_H_ */
