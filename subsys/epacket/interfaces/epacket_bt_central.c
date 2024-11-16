/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/gatt.h>
#include <infuse/bluetooth/gatt.h>
#include <infuse/epacket/interface/epacket_bt.h>
#include <infuse/epacket/interface/epacket_bt_central.h>

static const struct bt_uuid_128 command_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_COMMAND_VAL);
static const struct bt_uuid_128 data_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_DATA_VAL);
static const struct bt_uuid_128 logging_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_LOGGING_VAL);
static const struct bt_uuid *infuse_iot_characteristics[] = {
	(const void *)&command_uuid,
	(const void *)&data_uuid,
	(const void *)&logging_uuid,
};
static struct bt_gatt_remote_char
	infuse_iot_remote_cache_chars[ARRAY_SIZE(infuse_iot_characteristics)];
static struct bt_conn_auto_database_cache infuse_iot_remote_cache;
struct infuse_connection_state {
	struct bt_gatt_remote_char remote_info[ARRAY_SIZE(infuse_iot_characteristics)];
	struct bt_conn_auto_setup_params params;
	struct bt_conn_auto_discovery discovery;
	struct bt_gatt_subscribe_params command_sub;
	struct bt_gatt_subscribe_params data_sub;
	struct bt_gatt_subscribe_params logging_sub;
} infuse_conn;
static K_SEM_DEFINE(infuse_conn_available, 1, 1);
static K_SEM_DEFINE(infuse_conn_done, 0, 1);

struct bt_gatt_read_params_user {
	struct bt_gatt_read_params params;
	struct epacket_read_response *rsp;
	struct k_poll_signal *sig;
};

LOG_MODULE_REGISTER(epacket_bt_central, CONFIG_EPACKET_BT_CENTRAL_LOG_LEVEL);

static void conn_setup_cb(struct bt_conn *conn, int err, void *user_data)
{
	struct k_poll_signal *sig = user_data;

	/* Notify command handler */
	k_poll_signal_raise(sig, -err);
}

static uint8_t security_read_result(struct bt_conn *conn, uint8_t err,
				    struct bt_gatt_read_params *params, const void *data,
				    uint16_t length)
{
	struct bt_gatt_read_params_user *p =
		CONTAINER_OF(params, struct bt_gatt_read_params_user, params);
	const struct epacket_read_response *d = data;
	int rc;

	if (length == sizeof(struct epacket_read_response)) {
		*p->rsp = *d;
		rc = 0;
	} else {
		rc = -EINVAL;
	}
	k_poll_signal_raise(p->sig, rc);
	return BT_GATT_ITER_STOP;
}

static void conn_terminated_cb(struct bt_conn *conn, int reason, void *user_data)
{
	LOG_DBG("Infuse-IoT connection available");
	k_sem_give(&infuse_conn_available);
}

static uint8_t char_recv_func(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			      const void *data, uint16_t length)
{
	if (data == NULL) {
		/* Unsubscribed */
		return BT_GATT_ITER_CONTINUE;
	}
	LOG_INF("Received %d bytes", length);
	return BT_GATT_ITER_CONTINUE;
}

/* For now don't worry about waiting for the subscribe result.
 * We can assume that the subscription will take effect before we can use the connection.
 * If this is not the case, we can add blocking later.
 */
static int characteristic_subscribe(struct bt_conn *conn,
				    struct bt_gatt_remote_char *characteristic,
				    struct bt_gatt_subscribe_params *params, int subscribe)
{
	int rc;

	params->value_handle = characteristic->value_handle;
	params->ccc_handle = characteristic->ccc_handle;
	params->value = subscribe ? BT_GATT_CCC_NOTIFY : 0;
	params->subscribe = NULL;
	params->notify = char_recv_func;

	if (subscribe) {
		rc = bt_gatt_subscribe(conn, params);
		if (rc == -EALREADY) {
			rc = 0;
		}
	} else {
		rc = bt_gatt_unsubscribe(conn, params);
		if (rc == -EINVAL) {
			rc = 0;
		}
	}
	return rc;
}

int epacket_bt_gatt_connect(const bt_addr_le_t *peer, const struct bt_le_conn_param *conn_params,
			    uint32_t timeout_ms, struct bt_conn **conn_out,
			    struct epacket_read_response *security, bool subscribe_commands,
			    bool subscribe_data, bool subscribe_logging)
{
	struct k_poll_signal sig;
	struct bt_conn *conn;
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	unsigned int signaled;
	bool already = false;
	int conn_rc;
	int rc;

	*conn_out = NULL;
	k_poll_signal_init(&sig);

	/* Determine if connection already exists */
	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, peer);
	if (conn != NULL) {
		already = true;
		goto conn_created;
	}

	/* One valid connection at a time */
	if (k_sem_take(&infuse_conn_available, K_NO_WAIT) == -EBUSY) {
		rc = -ENOMEM;
		goto done;
	}

	/* Smallest connection interval for maximum data throughput */
	infuse_iot_remote_cache.remote_info = infuse_iot_remote_cache_chars;
	infuse_conn.params.conn_params =
		(struct bt_le_conn_param)BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	infuse_conn.params.create_timeout_ms = timeout_ms;
	infuse_conn.discovery.characteristics = infuse_iot_characteristics;
	infuse_conn.discovery.cache = &infuse_iot_remote_cache;
	infuse_conn.discovery.remote_info = infuse_conn.remote_info;
	infuse_conn.discovery.num_characteristics = ARRAY_SIZE(infuse_iot_characteristics);
	infuse_conn.params.conn_setup_cb = conn_setup_cb;
	infuse_conn.params.conn_terminated_cb = conn_terminated_cb;
	infuse_conn.params.user_data = &sig;

	/* Request the connection */
	conn = NULL;
	LOG_INF("Creating connection (timeout %d ms)", timeout_ms);
	rc = bt_conn_le_auto_setup(peer, &conn, &infuse_conn.params, &infuse_conn.discovery);
	if (rc < 0) {
		goto cleanup;
	}

	/* Wait for connection process to complete */
	k_poll(events, ARRAY_SIZE(events), K_FOREVER);
	k_poll_signal_check(&sig, &signaled, &conn_rc);
	if (!signaled) {
		LOG_ERR("Conn signal never raised?");
		rc = -ETIMEDOUT;
		goto cleanup;
	}
	if (conn_rc != 0) {
		/* Connection failed */
		rc = conn_rc;
		goto cleanup;
	}

conn_created:
	struct bt_gatt_read_params_user security_read = {
		.params =
			{
				.func = security_read_result,
				.handle_count = 1,
				.single.handle = infuse_conn.remote_info[0].value_handle,
				.single.offset = 0,
			},
		.rsp = security,
		.sig = &sig,
	};

	k_poll_signal_reset(&sig);
	events[0].state = K_POLL_STATE_NOT_READY;

	/* Read the data handle to get the security parameters */
	rc = bt_gatt_read(conn, &security_read.params);
	if (rc < 0) {
		goto cleanup;
	}
	k_poll(events, ARRAY_SIZE(events), K_MSEC(500));
	k_poll_signal_check(&sig, &signaled, &rc);
	if (!signaled || (rc != 0)) {
		rc = -EIO;
		goto cleanup;
	}

	/* Setup requested subscriptions */
	rc = characteristic_subscribe(conn, &infuse_conn.remote_info[0], &infuse_conn.command_sub,
				      subscribe_commands);
	if (rc == 0) {
		rc = characteristic_subscribe(conn, &infuse_conn.remote_info[1],
					      &infuse_conn.data_sub, subscribe_data);
	}
	if (rc == 0 && (infuse_conn.remote_info[2].ccc_handle != 0)) {
		rc = characteristic_subscribe(conn, &infuse_conn.remote_info[2],
					      &infuse_conn.logging_sub, subscribe_logging);
	}

cleanup:
	if (rc == 0) {
		*conn_out = conn;
	} else {
		/* Setup failed, attempt to cleanup connection */
		if (conn) {
			(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			bt_conn_unref(conn);
		}
		k_sem_give(&infuse_conn_available);
	}
done:
	if ((rc == 0) && already) {
		rc = 1;
	}
	return rc;
}
