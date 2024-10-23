/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/types.h>
#include <infuse/rpc/client.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

static struct rpc_client_ctx ctx;

LOG_MODULE_REGISTER(bt_ctlr_manager, LOG_LEVEL_INF);

int bt_controller_manager_init(void)
{
	KV_KEY_TYPE(KV_KEY_BLUETOOTH_CTLR_VERSION) bt_ctlr_ver;
	struct rpc_application_info_request req;
	struct rpc_application_info_response *rsp;
	struct net_buf *buf;
	int rc;

	rpc_client_init(&ctx, DEVICE_DT_GET(DT_INST(0, embeint_epacket_hci)));

	rc = rpc_client_command_sync(&ctx, RPC_ID_APPLICATION_INFO, &req, sizeof(req), K_NO_WAIT,
				     K_MSEC(200), &buf);
	if (rc < 0) {
		LOG_ERR("Failed to query version (%d)", rc);
		return rc;
	}

	/* Unregister from callbacks */
	rpc_client_cleanup(&ctx);

	rsp = (void *)buf->data;
	bt_ctlr_ver.application = rsp->application_id;
	bt_ctlr_ver.version.major = rsp->version.major;
	bt_ctlr_ver.version.minor = rsp->version.minor;
	bt_ctlr_ver.version.revision = rsp->version.revision;
	bt_ctlr_ver.version.build_num = rsp->version.build_num;
	net_buf_unref(buf);

	/* Write the version to storage so cloud can sync it */
	(void)KV_STORE_WRITE(KV_KEY_BLUETOOTH_CTLR_VERSION, &bt_ctlr_ver);
	return 0;
}
