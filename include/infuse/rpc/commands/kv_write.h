/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_KV_WRITE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_KV_WRITE_H_

#include <infuse/epacket/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Application level validation of external KV writes
 *
 * @param meta Metadata of the request packet
 * @param key Key that needs to be validated
 * @param data Pointer to the updated value, or NULL for a delete
 * @param len Length of the updated value, or 0 for a delete
 *
 * @retval true KV write is allowed
 * @retval false KV write is rejected
 */
bool infuse_rpc_command_kv_write_validate(struct epacket_rx_metadata *meta, uint16_t key,
					  const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_KV_WRITE_H_ */
