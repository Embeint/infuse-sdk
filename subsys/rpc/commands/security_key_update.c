/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/reboot.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct net_buf *rpc_command_security_key_update(struct net_buf *request)
{
	struct rpc_security_key_update_request *req = (void *)request->data;
	struct rpc_security_key_update_response rsp;
	const uint8_t *key_ptr;
	int rc = 0;

	switch (req->key_action) {
	case RPC_ENUM_KEY_ACTION_KEY_WRITE:
		key_ptr = req->key_bitstream;
		break;
	case RPC_ENUM_KEY_ACTION_KEY_DELETE:
		key_ptr = NULL;
		break;
	default:
		rc = -EINVAL;
		goto end;
	}

	switch (req->key_id) {
	case RPC_ENUM_KEY_ID_NETWORK_KEY:
		rc = infuse_security_network_key_write(req->key_global_identifier, key_ptr);
		break;
#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE
	case RPC_ENUM_KEY_ID_SECONDARY_NETWORK_KEY:
		rc = infuse_security_secondary_network_key_write(req->key_global_identifier,
								 key_ptr);
		break;
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE */
#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_REMOTE_ENABLE
	case RPC_ENUM_KEY_ID_SECONDARY_REMOTE_PUBLIC_KEY:
		if (key_ptr == NULL) {
			rc = kv_store_delete(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY);
		} else {
			rc = kv_store_write(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY, key_ptr,
					    sizeof(req->key_bitstream));
			/* 0 on success */
			rc = (rc == sizeof(req->key_bitstream)) ? 0 : rc;
		}
		break;
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_REMOTE_ENABLE */
	default:
		rc = -EINVAL;
	}

	/* Trigger reboot if requested */
	if ((rc == 0) && (req->reboot_delay > 0)) {
		infuse_reboot_delayed(INFUSE_REBOOT_CFG_CHANGE,
				      (uintptr_t)rpc_command_security_key_update, req->key_id,
				      K_SECONDS(req->reboot_delay));
	}

end:
	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
