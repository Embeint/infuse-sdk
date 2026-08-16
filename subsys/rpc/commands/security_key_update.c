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
#include <infuse/rpc/commands/security_key_update.h>
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
		rc = INFUSE_RPC_ERROR_KEY_ACTION_UNSUPPORTED;
		goto end;
	}

	/* Using EPACKET_AUTH_DEVICE instead of 2 doesn't work (preprocessor weirdness?) */
#if CONFIG_INFUSE_RPC_COMMAND_SECURITY_KEY_UPDATE_REQUIRED_AUTH < 2
	struct epacket_rx_metadata *meta = net_buf_user_data(request);

	if (meta->auth != EPACKET_AUTH_DEVICE) {
		/* If packet is not device authorised, defer to application for whether this should
		 * be run
		 */
		if (!infuse_rpc_command_security_authorised(meta, req)) {
			rc = INFUSE_RPC_ERROR_AUTHORIZATION_DENIED;
			goto end;
		}
	}
#endif /* CONFIG_INFUSE_RPC_COMMAND_SECURITY_KEY_UPDATE_REQUIRED_AUTH < 2 */

	switch (req->key_id) {
	case RPC_ENUM_KEY_ID_NETWORK_KEY:
		rc = infuse_security_network_key_write(req->key_global_identifier, key_ptr);
		if (rc < 0) {
			rc = key_ptr == NULL ? INFUSE_RPC_ERROR_KEY_DELETE_FAILED
					     : INFUSE_RPC_ERROR_KEY_WRITE_FAILED;
		}
		break;
	case RPC_ENUM_KEY_ID_DEVICE_PUBLIC_KEY:
		if (req->key_action == RPC_ENUM_KEY_ACTION_KEY_DELETE) {
			/* Deleting the root keypair (forcing a refresh) is the only valid option */
			rc = infuse_security_device_root_reset();
			if (rc < 0) {
				rc = INFUSE_RPC_ERROR_KEY_RESET_FAILED;
			}
		} else {
			rc = INFUSE_RPC_ERROR_KEY_WRITE_FORBIDDEN;
		}
		break;
#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE
	case RPC_ENUM_KEY_ID_SECONDARY_NETWORK_KEY:
		rc = infuse_security_secondary_network_key_write(req->key_global_identifier,
								 key_ptr);
		if (rc < 0) {
			rc = key_ptr == NULL ? INFUSE_RPC_ERROR_KEY_DELETE_FAILED
					     : INFUSE_RPC_ERROR_KEY_WRITE_FAILED;
		}
		break;
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE */
#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_REMOTE_ENABLE
	case RPC_ENUM_KEY_ID_SECONDARY_REMOTE_PUBLIC_KEY:
		/* When writing a new key, ensure an old one isn't cached */
		rc = infuse_security_secondary_device_key_reset();
		rc = (rc == -ENOENT) ? 0 : rc;
		if (rc < 0) {
			rc = INFUSE_RPC_ERROR_KEY_RESET_FAILED;
			goto end;
		}

		if (key_ptr == NULL) {
			rc = kv_store_delete(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY);
			if (rc < 0) {
				rc = INFUSE_RPC_ERROR_KEY_DELETE_FAILED;
			}
		} else {
			rc = kv_store_write(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY, key_ptr,
					    sizeof(req->key_bitstream));
			/* 0 on success */
			rc = (rc == sizeof(req->key_bitstream)) ? 0
								: INFUSE_RPC_ERROR_KEY_WRITE_FAILED;
		}
		break;
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_REMOTE_ENABLE */
	default:
		rc = INFUSE_RPC_ERROR_KEY_ID_UNSUPPORTED;
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
