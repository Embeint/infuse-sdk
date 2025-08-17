/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/gatt.h>

#include <infuse/bluetooth/gatt.h>
#include <infuse/work_q.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_bt.h>
#include <infuse/epacket/interface/epacket_bt_central.h>

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_BT_CONNECTIONS
#include <memfault/metrics/metrics.h>
#endif /* CONFIG_MEMFAULT_INFUSE_METRICS_BT_CONNECTIONS */

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_bt_central

#define PACKET_OVERHEAD (DT_INST_PROP(0, header_size) + DT_INST_PROP(0, footer_size))

static void conn_setup_cb(struct bt_conn *conn, int err, void *user_data);
static void conn_terminated_cb(struct bt_conn *conn, int reason, void *user_data);

enum {
	CHAR_COMMAND = 0,
	CHAR_DATA = 1,
	CHAR_LOGGING = 2,
	CHAR_NUM,
};

static const struct bt_uuid_128 command_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_COMMAND_VAL);
static const struct bt_uuid_128 data_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_DATA_VAL);
static const struct bt_uuid_128 logging_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_LOGGING_VAL);
static const struct bt_uuid *infuse_iot_characteristics[CHAR_NUM] = {
	[CHAR_COMMAND] = (const void *)&command_uuid,
	[CHAR_DATA] = (const void *)&data_uuid,
	[CHAR_LOGGING] = (const void *)&logging_uuid,
};

BT_CONN_AUTO_CACHE(infuse_iot_remote_cache, CHAR_NUM);

struct infuse_connection_state {
	struct bt_gatt_remote_char remote_info[CHAR_NUM];
	struct bt_conn_auto_discovery discovery;
	struct bt_gatt_subscribe_params subs[CHAR_NUM];
	struct k_poll_signal sig;
	struct k_work_delayable idle_worker;
	struct k_work_delayable term_worker;
	k_timeout_t inactivity_timeout;
	uint32_t network_id;
} infuse_conn[CONFIG_BT_MAX_CONN];

struct bt_gatt_read_params_user {
	struct bt_gatt_read_params params;
	struct epacket_read_response *rsp;
	struct k_poll_signal *sig;
};

static const struct bt_conn_auto_setup_cb callbacks = {
	.conn_setup_cb = conn_setup_cb,
	.conn_terminated_cb = conn_terminated_cb,
};

struct epacket_bt_central_data {
	struct epacket_interface_common_data common_data;
};

LOG_MODULE_REGISTER(epacket_bt_central, CONFIG_EPACKET_BT_CENTRAL_LOG_LEVEL);

static void conn_setup_cb(struct bt_conn *conn, int err, void *user_data)
{
	uint8_t idx = bt_conn_index(conn);

	/* Notify command handler */
	k_poll_signal_raise(&infuse_conn[idx].sig, err);
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
	uint8_t idx = bt_conn_index(conn);

	/* Cancel any pending timeouts */
	k_work_cancel_delayable(&infuse_conn[idx].idle_worker);
	k_work_cancel_delayable(&infuse_conn[idx].term_worker);
}

uint8_t epacket_bt_gatt_notify_recv_func(struct bt_conn *conn,
					 struct bt_gatt_subscribe_params *params, const void *data,
					 uint16_t length)
{
	struct infuse_connection_state *s;
	struct epacket_rx_metadata *meta;
	struct net_buf *rx_buffer;
	uint8_t idx;

	if (data == NULL) {
		/* Unsubscribed */
		return BT_GATT_ITER_CONTINUE;
	}

	LOG_DBG("Received %d bytes", length);
	rx_buffer = epacket_alloc_rx(K_NO_WAIT);
	if (rx_buffer == NULL) {
		LOG_WRN("Buffer claim timeout");
		return BT_GATT_ITER_CONTINUE;
	}
	if (length > net_buf_tailroom(rx_buffer)) {
		LOG_WRN("Insufficient space (%d > %d)", length, net_buf_tailroom(rx_buffer));
		net_buf_unref(rx_buffer);
		return BT_GATT_ITER_CONTINUE;
	}

	/* Copy payload across */
	net_buf_add_mem(rx_buffer, data, length);

	/* Save metadata */
	meta = net_buf_user_data(rx_buffer);
	meta->interface = DEVICE_DT_INST_GET(0);
	meta->interface_id = EPACKET_INTERFACE_BT_CENTRAL;
	meta->interface_address.bluetooth = *bt_conn_get_dst(conn);

#ifdef CONFIG_BT_CONN_AUTO_RSSI
	meta->rssi = bt_conn_rssi(conn);
#else
	meta->rssi = 0;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */

	/* Refresh inactivity timeout (on command or data characteristics) */
	idx = bt_conn_index(conn);
	s = &infuse_conn[idx];
	if (!K_TIMEOUT_EQ(s->inactivity_timeout, K_FOREVER) &&
	    (params->value_handle != s->remote_info[CHAR_LOGGING].value_handle)) {
		k_work_reschedule(&s->idle_worker, s->inactivity_timeout);
	}

	/* Hand off to ePacket core */
	epacket_raw_receive_handler(rx_buffer);

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
	params->notify = epacket_bt_gatt_notify_recv_func;

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

/* Internal API, but should be safe as it is just the opposite of `bt_conn_index` */
struct bt_conn *bt_conn_lookup_index(uint8_t index);

static void do_disconnect(struct infuse_connection_state *s, char *reason)
{

	uint8_t state_idx = ARRAY_INDEX(infuse_conn, s);
	struct bt_conn *conn = bt_conn_lookup_index(state_idx);
	int rc;

#ifdef CONFIG_ASSERT
	__ASSERT_NO_MSG(conn != NULL);
#else
	if (conn == NULL) {
		LOG_DBG("No conn found");
		return;
	}
#endif

	LOG_INF("Connection %s, disconnecting", reason);
	/* Trigger the disconnection, no need to wait for it to complete */
	rc = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (rc != 0) {
		LOG_ERR("Failed to trigger disconnection (%d)", rc);
	}

	/* Release the reference from bt_conn_lookup_index */
	bt_conn_unref(conn);
}

static void bt_conn_idle(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct infuse_connection_state *s =
		CONTAINER_OF(dwork, struct infuse_connection_state, idle_worker);

	do_disconnect(s, "idle");
}

static void bt_conn_timeout(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct infuse_connection_state *s =
		CONTAINER_OF(dwork, struct infuse_connection_state, term_worker);

	do_disconnect(s, "timeout");
}

int epacket_bt_gatt_connect(struct bt_conn **conn_out,
			    struct epacket_bt_gatt_connect_params *params,
			    struct epacket_read_response *security)
{
	const struct bt_conn_le_create_param create_param = {
		.interval = BT_GAP_SCAN_FAST_INTERVAL_MIN,
		.window = BT_GAP_SCAN_FAST_WINDOW,
		.timeout = params->conn_timeout_ms / 10,
	};
	struct k_poll_event poll_event;
	struct infuse_connection_state *s;
	unsigned int signaled;
	struct bt_conn *conn;
	uint8_t idx;
	int conn_rc;
	int rc = 0;

	*conn_out = NULL;

	/* Determine if connection already exists */
	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &params->peer);
	if (conn != NULL) {
		idx = bt_conn_index(conn);
		s = &infuse_conn[idx];
#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_BT_CONNECTIONS
		(void)MEMFAULT_METRIC_ADD(epacket_bt_central_conn_already, 1);
#endif /* CONFIG_MEMFAULT_INFUSE_METRICS_BT_CONNECTIONS */
		goto conn_created;
	}

	/* Create the connection */
	conn = NULL;
	LOG_INF("Creating connection (timeout %d ms)", params->conn_timeout_ms);
	rc = bt_conn_le_create(&params->peer, &create_param, &params->conn_params, &conn);
	if (rc < 0) {
		return rc;
	}
	idx = bt_conn_index(conn);
	s = &infuse_conn[idx];

	k_poll_signal_init(&s->sig);
	k_work_init_delayable(&s->idle_worker, bt_conn_idle);
	k_work_init_delayable(&s->term_worker, bt_conn_timeout);
	s->discovery.characteristics = infuse_iot_characteristics;
	s->discovery.cache = &infuse_iot_remote_cache;
	s->discovery.remote_info = s->remote_info;
	s->discovery.num_characteristics = CHAR_NUM;

	/* Register for the connection to be automatically setup */
	bt_conn_le_auto_setup(conn, &s->discovery, &callbacks, params->preferred_phy);

	/* Wait for connection process to complete */
	poll_event = (struct k_poll_event)K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &s->sig);

	k_poll(&poll_event, 1, K_FOREVER);
	k_poll_signal_check(&s->sig, &signaled, &conn_rc);
	__ASSERT_NO_MSG(signaled != 0);
	if (conn_rc != 0) {
		/* Connection failed */
		rc = conn_rc;
		goto cleanup;
	}

conn_created:
	/* Connection available, update the timeouts if specified */
	s->inactivity_timeout = params->inactivity_timeout;
	if (rc == 0) {
		if (!K_TIMEOUT_EQ(params->inactivity_timeout, K_FOREVER)) {
			k_work_reschedule(&s->idle_worker, params->inactivity_timeout);
		}
		if (!K_TIMEOUT_EQ(params->absolute_timeout, K_FOREVER)) {
			k_work_reschedule(&s->term_worker, params->absolute_timeout);
		}
	}

	struct bt_gatt_read_params_user security_read = {
		.params =
			{
				.func = security_read_result,
				.handle_count = 1,
				.single.handle = s->remote_info[CHAR_COMMAND].value_handle,
				.single.offset = 0,
			},
		.rsp = security,
		.sig = &s->sig,
	};

	k_poll_signal_reset(&s->sig);
	poll_event = (struct k_poll_event)K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &s->sig);

	/* Read the data handle to get the security parameters */
	rc = bt_gatt_read(conn, &security_read.params);
	if (rc < 0) {
		goto cleanup;
	}

	/* The bt_gatt_read_params structure MUST be valid until the callback is run */
	k_poll(&poll_event, 1, K_FOREVER);
	k_poll_signal_check(&s->sig, &signaled, &rc);
	__ASSERT_NO_MSG(signaled != 0);
	if (rc != 0) {
		goto cleanup;
	}

	/* Store the network ID */
	s->network_id = security->network_id;

	/* Setup requested subscriptions */
	rc = characteristic_subscribe(conn, &s->remote_info[CHAR_COMMAND], &s->subs[CHAR_COMMAND],
				      params->subscribe_commands);
	if (rc == 0) {
		rc = characteristic_subscribe(conn, &s->remote_info[CHAR_DATA], &s->subs[CHAR_DATA],
					      params->subscribe_data);
	}
	if (rc == 0 && (s->remote_info[CHAR_LOGGING].ccc_handle != 0)) {
		rc = characteristic_subscribe(conn, &s->remote_info[CHAR_LOGGING],
					      &s->subs[CHAR_LOGGING], params->subscribe_logging);
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
	}
	return rc;
}

static int infuse_send_rate_request(struct bt_conn *conn, void *data, size_t len)
{
	struct infuse_connection_state *s;
	struct bt_conn_info info;
	uint8_t conn_idx;
	uint16_t handle;
	int rc;

	/* Only run for connection objects in the connected state */
	if (bt_conn_get_info(conn, &info) < 0) {
		return -EINVAL;
	}
	if (info.state != BT_CONN_STATE_CONNECTED) {
		return -ENOTCONN;
	}

	/* Get connection state */
	conn_idx = bt_conn_index(conn);
	s = &infuse_conn[conn_idx];
	/* Get the command characteristic */
	handle = s->remote_info[CHAR_COMMAND].value_handle;
	if (handle == 0x0000) {
		/* Connection does not have this characteristic */
		return -EINVAL;
	}

	/* Write the request to the device */
	rc = bt_gatt_write_without_response(conn, handle, data, len, false);
	if (rc != 0) {
		LOG_WRN("Failed to write rate limit request (%d)", rc);
	}
	return rc;
}

static void infuse_send_rate_limit_request_no_ret(struct bt_conn *conn, void *data)
{
	(void)infuse_send_rate_request(conn, data, sizeof(struct epacket_rate_limit_req));
}

static uint8_t rate_limit_req_ms;
static void do_rate_limit_request(struct k_work *work)
{
	struct epacket_rate_limit_req request = {
		.magic = EPACKET_RATE_LIMIT_REQ_MAGIC,
		.delay_ms = rate_limit_req_ms,
	};

	bt_conn_foreach(BT_CONN_TYPE_LE, infuse_send_rate_limit_request_no_ret, &request);
}
static K_WORK_DEFINE(rate_limit_worker, do_rate_limit_request);

void epacket_bt_gatt_rate_limit_request(uint8_t delay_ms)
{
	/* Run requests from Infuse workqueue to prevent blocking caller */
	rate_limit_req_ms = delay_ms;
	infuse_work_submit(&rate_limit_worker);
}

int epacket_bt_gatt_rate_throughput_request(struct bt_conn *conn, uint16_t throughput_kbps)
{

	struct epacket_rate_throughput_req request = {
		.magic = EPACKET_RATE_LIMIT_REQ_MAGIC,
		.target_throughput_kbps = throughput_kbps,
	};

	return infuse_send_rate_request(conn, &request, sizeof(struct epacket_rate_throughput_req));
}

static void epacket_bt_central_send(const struct device *dev, struct net_buf *buf)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	struct infuse_connection_state *s;
	struct bt_conn *conn = NULL;
	uint8_t conn_idx;
	uint16_t handle;
	int rc;

	/* Find the destination remote device */
	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &meta->interface_address.bluetooth);
	if (conn == NULL) {
		LOG_DBG("Connection lookup failed");
		rc = -ENOTCONN;
		goto cleanup;
	}

	/* Get connection state */
	conn_idx = bt_conn_index(conn);
	s = &infuse_conn[conn_idx];

	/* Encrypt the payload */
	if (epacket_bt_gatt_encrypt(buf, s->network_id) < 0) {
		LOG_WRN("Failed to encrypt");
		rc = -EIO;
		goto cleanup;
	}

	/* Get appropriate characteristic handle and validate */
	switch (meta->type) {
	case INFUSE_RPC_CMD:
	case INFUSE_RPC_DATA:
		handle = s->remote_info[CHAR_COMMAND].value_handle;
		break;
	default:
		handle = s->remote_info[CHAR_DATA].value_handle;
		break;
	}
	if (handle == 0x0000) {
		/* Required characteristic not found on remote */
		rc = -ENOTSUP;
		goto cleanup;
	}

	/* Write the data to the peer */
	LOG_DBG("Writing %d bytes to handle %d on conn %p", buf->len, handle, (void *)conn);
	rc = bt_gatt_write_without_response(conn, handle, buf->data, buf->len, false);

	/* Refresh inactivity timeout */
	if (!K_TIMEOUT_EQ(s->inactivity_timeout, K_FOREVER)) {
		k_work_reschedule(&s->idle_worker, s->inactivity_timeout);
	}

cleanup:
	epacket_notify_tx_result(dev, buf, rc);
	net_buf_unref(buf);
	if (conn != NULL) {
		/* Cleanup resources */
		bt_conn_unref(conn);
	}
}

static int epacket_bt_central_init(const struct device *dev)
{
	epacket_interface_common_init(dev);
	return 0;
}

static const struct epacket_interface_api bt_gatt_api = {
	.send = epacket_bt_central_send,
};

BUILD_ASSERT(244 == DT_INST_PROP(0, max_packet_size));
static struct epacket_bt_central_data epacket_bt_central_data_inst;
static const struct epacket_interface_common_config epacket_bt_central_config = {
	.max_packet_size = EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(0)),
	.header_size = DT_INST_PROP(0, header_size),
	.footer_size = DT_INST_PROP(0, footer_size),
};
DEVICE_DT_DEFINE(DT_DRV_INST(0), epacket_bt_central_init, NULL, &epacket_bt_central_data_inst,
		 &epacket_bt_central_config, POST_KERNEL, 0, &bt_gatt_api);
