/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/hwinfo.h>

#include <infuse/identifiers.h>
#include <infuse/security.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

#include <psa/crypto.h>
#include <mbedtls/platform_util.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct public_key_helper {
	uint8_t id;
	int (*retrieve)(uint8_t key[32]);
};

const struct public_key_helper public_key_list[] = {
	{
		.id = RPC_ENUM_KEY_ID_DEVICE_PUBLIC_KEY,
		.retrieve = infuse_security_device_public_key,
	},
	{
		.id = RPC_ENUM_KEY_ID_CLOUD_PUBLIC_KEY,
		.retrieve = infuse_security_cloud_public_key,
	},
#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_REMOTE_ENABLE
	{
		.id = RPC_ENUM_KEY_ID_SECONDARY_REMOTE_PUBLIC_KEY,
		.retrieve = infuse_security_secondary_remote_public_key,
	},
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_REMOTE_ENABLE */
};

struct net_buf *rpc_command_security_public_keys(struct net_buf *request)
{
	struct rpc_security_public_keys_request *req = (void *)request->data;
	struct rpc_security_public_keys_response rsp_header = {0};
	struct rpc_security_public_keys_response *rsp_p;
	struct rpc_struct_public_key_info_256bit *key;
	struct net_buf *rsp_buf;

	/* Allocate response */
	rsp_buf = rpc_response_simple_req(request, 0, &rsp_header, sizeof(rsp_header));
	rsp_p = (void *)rsp_buf->data;
	rsp_p->keys_total = ARRAY_SIZE(public_key_list);

	/* Iterate over public keys */
	for (int i = 0; i < ARRAY_SIZE(public_key_list); i++) {
		if ((i < req->skip) || (net_buf_tailroom(rsp_buf) < sizeof(*key))) {
			continue;
		}
		key = net_buf_add(rsp_buf, sizeof(*key));
		if (public_key_list[i].retrieve(key->key) == 0) {
			key->id = public_key_list[i].id;
			rsp_p->keys_included += 1;
		} else {
			/* Query failed, remove from output */
			net_buf_remove_mem(rsp_buf, sizeof(*key));
			rsp_p->keys_total -= 1;
		}
	}

	/* Return the response */
	return rsp_buf;
}
