/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_KEY_UPDATE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_KEY_UPDATE_H_

#include <infuse/epacket/packet.h>
#include <infuse/rpc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Application level authorisation check for `security_key_update`
 *
 * Only required if @a CONFIG_INFUSE_RPC_COMMAND_SECURITY_KEY_UPDATE_REQUIRED_AUTH
 * allows non-device authorised packets.
 *
 * @param meta Metadata of the request
 * @param req Request parameters
 *
 * @retval true Command can run
 * @retval false Command cannot run
 */
bool infuse_rpc_command_security_authorised(struct epacket_rx_metadata *meta,
					    struct rpc_security_key_update_request *req);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_RPC_COMMANDS_SECURITY_KEY_UPDATE_H_ */
