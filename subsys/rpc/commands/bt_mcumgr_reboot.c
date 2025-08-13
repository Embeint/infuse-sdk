/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#include "../../../zephyr/subsys/mgmt/mcumgr/transport/include/mgmt/mcumgr/transport/smp_internal.h"

#include <infuse/bluetooth/gatt.h>
#include <infuse/epacket/interface/epacket_bt_central.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

#include "common_bt.h"

LOG_MODULE_DECLARE(rpc_server);

static const struct bt_uuid_128 mcumgr_uuid = BT_UUID_INIT_128(SMP_BT_CHR_UUID_VAL);

struct bt_gatt_write_params_meta {
	struct bt_gatt_write_params params;
	struct k_poll_signal *sig;
};

static void conn_setup_cb(struct bt_conn *conn, int err, void *user_data)
{
	struct k_poll_signal *sig = user_data;

	ARG_UNUSED(conn);

	k_poll_signal_raise(sig, err);
}

static void gatt_write_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	struct bt_gatt_write_params_meta *meta =
		CONTAINER_OF(params, struct bt_gatt_write_params_meta, params);

	ARG_UNUSED(conn);

	k_poll_signal_raise(meta->sig, err);
}

struct net_buf *rpc_command_bt_mcumgr_reboot(struct net_buf *request)
{
	struct rpc_bt_mcumgr_reboot_request *req = (void *)request->data;
	struct rpc_bt_mcumgr_reboot_response rsp = {};
	bt_addr_le_t peer = bt_addr_infuse_to_zephyr(&req->peer);
	const struct bt_conn_le_create_param create_param = {
		.interval = BT_GAP_SCAN_FAST_INTERVAL_MIN,
		.window = BT_GAP_SCAN_FAST_WINDOW,
		.timeout = req->conn_timeout_ms / 10,
	};
	const struct bt_le_conn_param conn_param = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	const struct bt_uuid *bt_characteristics[] = {
		(const void *)&mcumgr_uuid,
	};
	struct bt_gatt_remote_char remote_info = {0};
	struct bt_conn_auto_discovery discovery = {
		.characteristics = bt_characteristics,
		.cache = NULL,
		.remote_info = &remote_info,
		.num_characteristics = 1,
	};
	struct k_poll_signal sig;
	struct k_poll_event poll_event =
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);
	struct bt_conn_auto_setup_cb callbacks = {
		.conn_setup_cb = conn_setup_cb,
		.user_data = &sig,
	};
	struct bt_conn *conn = NULL;
	unsigned int signaled;
	int rc;

	k_poll_signal_init(&sig);

	/* Create the connection */
	rc = bt_conn_le_create(&peer, &create_param, &conn_param, &conn);
	if (rc != 0) {
		goto end;
	}

	/* Register for the connection to be automatically setup */
	bt_conn_le_auto_setup(conn, &discovery, &callbacks, BT_GAP_LE_PHY_NONE);

	/* Wait for connection process to complete */
	k_poll(&poll_event, 1, K_FOREVER);
	k_poll_signal_check(&sig, &signaled, &rc);
	__ASSERT_NO_MSG(signaled != 0);

	/* Cleanup callbacks since we aren't sticking around until the connection end */
	bt_conn_le_auto_setup(conn, NULL, NULL, BT_GAP_LE_PHY_NONE);
	if (rc != 0) {
		goto end;
	}

	/* Validate characteristic exists */
	if (remote_info.value_handle == 0x0000) {
		LOG_WRN("MCUMGR characteristic not found");
		rc = -EBADF;
		goto end;
	}

	/* Write the reset command across the connection */
	const struct smp_hdr reboot_cmd = {
		.nh_op = MGMT_OP_WRITE,
		.nh_version = 1,
		.nh_flags = 0,
		.nh_len = 0,
		.nh_group = MGMT_GROUP_ID_OS,
		.nh_seq = 0,
		.nh_id = OS_MGMT_ID_RESET,
	};
	struct bt_gatt_write_params_meta params = {
		.params =
			{
				.func = gatt_write_cb,
				.handle = remote_info.value_handle,
				.offset = 0,
				.data = &reboot_cmd,
				.length = sizeof(reboot_cmd),
			},
		.sig = &sig,
	};

	k_poll_signal_reset(&sig);
	rc = bt_gatt_write(conn, &params.params);
	if (rc < 0) {
		goto end;
	}

	/* Wait for the write to complete */
	k_poll(&poll_event, 1, K_FOREVER);
	k_poll_signal_check(&sig, &signaled, &rc);
	__ASSERT_NO_MSG(signaled != 0);

end:
	if (conn) {
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(conn);
	}

	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
