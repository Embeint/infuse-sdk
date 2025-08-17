/**
 * @file
 * @brief
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_RPC_COMMANDS_COMMON_NET_QUERY_H_
#define INFUSE_SDK_SUBSYS_RPC_COMMANDS_COMMON_NET_QUERY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/net/net_if.h>
#include <infuse/rpc/types.h>

void rpc_common_net_query(struct net_if *iface, struct rpc_struct_network_state *out);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_RPC_COMMANDS_COMMON_NET_QUERY_H_ */
