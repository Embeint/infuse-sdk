/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_RPC_COMMANDS_COMMON_BT_H_
#define INFUSE_SDK_SUBSYS_RPC_COMMANDS_COMMON_BT_H_

#include <zephyr/bluetooth/addr.h>

#include <infuse/rpc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bt_addr_le_t bt_addr_infuse_to_zephyr(struct rpc_struct_bt_addr_le *addr)
{
	return (bt_addr_le_t){
		.type = addr->type,
		.a.val =
			{
				addr->val[0],
				addr->val[1],
				addr->val[2],
				addr->val[3],
				addr->val[4],
				addr->val[5],
			},
	};
}

/* Implementation of BT_FILE_COPY_BASIC */
int rpc_command_bt_file_copy_basic_run(struct rpc_bt_file_copy_basic_request *req,
				       struct rpc_bt_file_copy_basic_response *rsp);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_RPC_COMMANDS_COMMON_BT_H_ */
