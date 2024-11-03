/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/init.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/bluetooth/gatt.h>
#include <infuse/task_runner/runner.h>

static struct bt_gatt_state {
#ifdef CONFIG_BT_CONN_AUTO_RSSI
	struct k_work_delayable rssi_query;
	int8_t rssi;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */
#ifdef CONFIG_BT_GATT_CLIENT
	struct bt_conn_auto_setup_params *params;
#endif /* CONFIG_BT_GATT_CLIENT */
} state[CONFIG_BT_MAX_CONN];

LOG_MODULE_REGISTER(infuse_gatt, LOG_LEVEL_INF);

const char *bt_addr_le_str(const bt_addr_le_t *addr);

#ifdef CONFIG_BT_GATT_CLIENT

static const struct bt_uuid_16 ccc_uuid = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);
static const struct bt_uuid_16 db_hash_uuid = BT_UUID_INIT_16(BT_UUID_GATT_DB_HASH_VAL);
static struct bt_gatt_exchange_params mtu_exchange_params;
static struct bt_gatt_read_params db_read_params;

int bt_conn_le_auto_setup(const bt_addr_le_t *addr, struct bt_conn **conn,
			  struct bt_conn_auto_setup_params *params)
{
	const struct bt_conn_le_create_param create_param = {
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_INTERVAL,
		/* 10ms units */
		.timeout = params->create_timeout_ms / 10,
	};
	int rc;

	rc = bt_conn_le_create(addr, &create_param, &params->conn_params, conn);
	if (rc == 0) {
		state[bt_conn_index(*conn)].params = params;
	}
	return rc;
}

static void connection_done(struct bt_conn *conn)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	/* Run user callback */
	s->params->conn_setup_cb(conn, 0, s->params->user_data);
}

static void connection_error(struct bt_conn *conn, int err)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	LOG_ERR("Connection setup failed (%d)", err);

	/* Run user callback */
	s->params->conn_setup_cb(conn, err, s->params->user_data);

	/* Clear state */
	s->params = NULL;
}

static void discovery_error(struct bt_conn *conn, int err)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	/* Clear cache */
	memset(s->params->discovery.db_hash, 0x00, sizeof(s->params->discovery.db_hash));
	connection_error(conn, err);
}

static void descriptor_discovery(struct bt_conn *conn);

static uint8_t ccc_discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       struct bt_gatt_discover_params *params)
{
	if (!attr) {
		/* Continue discovery */
		descriptor_discovery(conn);
		return BT_GATT_ITER_STOP;
	}

	struct bt_gatt_ccc *ccc = (struct bt_gatt_ccc *)attr->user_data;
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];
	struct bt_gatt_remote_char *remote_char;

	LOG_DBG("Discovered CCC handle: %u Flags %04X", attr->handle, ccc->flags);

	/* Assign CCC handle to appropriate characteristic */
	for (int i = 0; i < s->params->discovery.num_characteristics; i++) {
		remote_char = &s->params->discovery.characteristics[i];

		if (IN_RANGE(attr->handle, remote_char->attr_start_handle,
			     remote_char->attr_end_handle)) {
			remote_char->ccc_handle = attr->handle;
			break;
		}
	}
	return BT_GATT_ITER_CONTINUE;
}

static void descriptor_discovery(struct bt_conn *conn)
{
	static struct bt_gatt_discover_params descriptor_params;
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];
	struct bt_gatt_remote_char *remote_char;
	int rc;

	/* Find characteristic without CCC yet found */
	for (int i = 0; i < s->params->discovery.num_characteristics; i++) {
		remote_char = &s->params->discovery.characteristics[i];

		if (remote_char->attr_start_handle == 0x0000) {
			/* Characteristic was not found */
			continue;
		}
		if (remote_char->ccc_handle != 0x0000) {
			/* Already found */
			continue;
		}
		if (!(remote_char->properties & (BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_INDICATE))) {
			/* Notification and Indication not supported */
			continue;
		}

		/* Set up the discovery parameters for characteristics */
		descriptor_params.uuid = (struct bt_uuid *)&ccc_uuid;
		descriptor_params.func = ccc_discover_cb;
		descriptor_params.start_handle = remote_char->attr_start_handle + 1;
		descriptor_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		descriptor_params.type = BT_GATT_DISCOVER_STD_CHAR_DESC;

		/* Start discovery procedure */
		rc = bt_gatt_discover(conn, &descriptor_params);
		if (rc < 0) {
			discovery_error(conn, rc);
		}
		return;
	}

	LOG_INF("Characteristic discovery complete");
	for (int i = 0; i < s->params->discovery.num_characteristics; i++) {
		remote_char = &s->params->discovery.characteristics[i];
		LOG_INF("\t%d: Range (%5d - %5d) Value %d CCC %d", i,
			remote_char->attr_start_handle, remote_char->attr_end_handle,
			remote_char->value_handle, remote_char->ccc_handle);
	}

	/* Connection has been setup and discovered */
	connection_done(conn);
}

static uint8_t char_discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *params)
{
	if (!attr) {
		descriptor_discovery(conn);
		return BT_GATT_ITER_STOP;
	}

	/* Extract characteristic information from the attribute */
	struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];
	struct bt_gatt_remote_char *remote_char;

	LOG_DBG("ATTR Handle %d Value Handle %d Properties %02X", attr->handle, chrc->value_handle,
		chrc->properties);

	/* Determine if this characteristic is one we are looking for */
	for (int i = 0; i < s->params->discovery.num_characteristics; i++) {
		remote_char = &s->params->discovery.characteristics[i];

		if (remote_char->attr_start_handle != 0x0000) {
			/* Update the previous characteristic end handle if appropriate */
			if (remote_char->attr_end_handle == BT_ATT_LAST_ATTRIBUTE_HANDLE) {
				remote_char->attr_end_handle = attr->handle - 1;
			}
			/* Already found */
			continue;
		}
		if (bt_uuid_cmp(remote_char->uuid, chrc->uuid) == 0) {
			remote_char->properties = chrc->properties;
			remote_char->attr_start_handle = attr->handle;
			remote_char->value_handle = chrc->value_handle;
			break;
		}
	}

	/* Are we still looking for any characteristics? */
	for (int i = 0; i < s->params->discovery.num_characteristics; i++) {
		remote_char = &s->params->discovery.characteristics[i];
		if ((remote_char->attr_start_handle == 0x0000) ||
		    (remote_char->attr_end_handle == BT_ATT_LAST_ATTRIBUTE_HANDLE)) {
			/* Still looking for information */
			return BT_GATT_ITER_CONTINUE;
		}
	}

	/* All ATTR handles have been found, find relevant CCC handles */
	descriptor_discovery(conn);
	return BT_GATT_ITER_STOP;
}

static void characteristic_discovery(struct bt_conn *conn)
{
	static struct bt_gatt_discover_params characteristic_params;
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];
	int rc;

	/* Reset cached handles */
	for (int i = 0; i < s->params->discovery.num_characteristics; i++) {
		s->params->discovery.characteristics[i].attr_start_handle = 0x0000;
		s->params->discovery.characteristics[i].attr_end_handle =
			BT_ATT_LAST_ATTRIBUTE_HANDLE;
		s->params->discovery.characteristics[i].value_handle = 0x0000;
		s->params->discovery.characteristics[i].ccc_handle = 0x0000;
	}

	/* Set up the discovery parameters for characteristics */
	characteristic_params.uuid = NULL;
	characteristic_params.func = char_discover_cb;
	characteristic_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	characteristic_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	characteristic_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	/* Start discovery procedure */
	rc = bt_gatt_discover(conn, &characteristic_params);
	if (rc) {
		discovery_error(conn, rc);
	}
}

uint8_t gatt_db_hash_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
			const void *data, uint16_t length)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	if ((err) || (length != sizeof(s->params->discovery.db_hash))) {
		LOG_WRN("Failed to read DB hash (%d)", err);
		memset(s->params->discovery.db_hash, 0x00, sizeof(s->params->discovery.db_hash));
	} else {
		/* If DB hash matches cached value, we can skip discovery */
		if (memcmp(s->params->discovery.db_hash, data, length) == 0) {
			LOG_INF("Characteristic handles from %s", "cache");
			connection_done(conn);
			return BT_GATT_ITER_STOP;
		}
		LOG_INF("Characteristic handles from %s", "discovery");
		/* Copy DB hash into conn params */
		memcpy(s->params->discovery.db_hash, data, length);
	}

	/* Start characteristic discovery */
	characteristic_discovery(conn);
	return BT_GATT_ITER_STOP;
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	if (err) {
		connection_error(conn, err);
		return;
	}

	LOG_DBG("MTU exchange %s (%u)", err == 0U ? "successful" : "failed", bt_gatt_get_mtu(conn));

	/* Read the remote database hash by UUID */
	db_read_params.func = gatt_db_hash_cb;
	db_read_params.handle_count = 0;
	db_read_params.by_uuid.uuid = (struct bt_uuid *)&db_hash_uuid;
	db_read_params.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	db_read_params.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;

	err = bt_gatt_read(conn, &db_read_params);
	if (err < 0) {
		connection_error(conn, err);
	}
}

static void central_conn_setup(struct bt_conn *conn)
{
	int rc;

	/* First action, request MTU update */
	mtu_exchange_params.func = mtu_exchange_cb;
	rc = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
	if (rc < 0) {
		connection_error(conn, rc);
	}
}

#endif /* CONFIG_BT_GATT_CLIENT */

static void connected(struct bt_conn *conn, uint8_t err)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	ARG_UNUSED(s);

	if (err == BT_HCI_ERR_SUCCESS) {
		LOG_INF("Connected to %s", bt_addr_le_str(dst));
	} else {
		LOG_WRN("Connection to %s failed (error 0x%02X)", bt_addr_le_str(dst), err);
	}
	if (err) {
#ifdef CONFIG_BT_GATT_CLIENT
		if (s->params) {
			connection_error(conn, err);
		}
#endif /* CONFIG_BT_GATT_CLIENT */
		return;
	}

#ifdef CONFIG_BT_GATT_CLIENT
	struct bt_conn_info info;
	int rc;

	/* Only handle connections initiated through bt_conn_le_auto_setup */
	if (s->params != NULL) {
		rc = bt_conn_get_info(conn, &info);
		if (rc < 0) {
			LOG_ERR("Failed to get connection info");
			connection_error(conn, rc);
			return;
		}
		if (info.role == BT_CONN_ROLE_CENTRAL) {
			central_conn_setup(conn);
		}
	}
#endif /* CONFIG_BT_GATT_CLIENT */

#ifdef CONFIG_BT_CONN_AUTO_RSSI
	/* Small delay to give controller a chance to finish setup */
	k_work_reschedule_for_queue(task_runner_work_q(), &state[bt_conn_index(conn)].rssi_query,
				    K_MSEC(10));
#endif /* CONFIG_BT_CONN_AUTO_RSSI */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	ARG_UNUSED(s);

#ifdef CONFIG_BT_CONN_AUTO_RSSI
	k_work_cancel_delayable(&s->rssi_query);
	s->rssi = 0;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */
#ifdef CONFIG_BT_GATT_CLIENT
	if (s->params) {
		if (s->params->conn_terminated_cb) {
			s->params->conn_terminated_cb(conn, reason, s->params->user_data);
		}
		s->params = NULL;
	}
#endif /* CONFIG_BT_GATT_CLIENT */

	LOG_INF("Disconnected from %s (reason 0x%02X)", bt_addr_le_str(dst), reason);
}

#ifdef CONFIG_BT_CONN_AUTO_RSSI

/* Internal API, but should be safe as it is just the opposite of `bt_conn_index` */
struct bt_conn *bt_conn_lookup_index(uint8_t index);

static void rssi_query_worker(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct bt_gatt_state *conn_state = CONTAINER_OF(dwork, struct bt_gatt_state, rssi_query);
	struct net_buf *buf, *rsp = NULL;
	struct bt_hci_cp_read_rssi *cp;
	struct bt_hci_rp_read_rssi *rp;
	uint8_t conn_idx = ARRAY_INDEX(state, conn_state);
	struct bt_conn *conn = bt_conn_lookup_index(conn_idx);
	uint16_t handle;
	int rc;

	rc = bt_hci_get_conn_handle(conn, &handle);
	if (rc < 0) {
		/* Expected to happen if running on a connection that has terminated.
		 * Don't requeue.
		 */
		LOG_DBG("Failed to get handle (%d)", rc);
		bt_conn_unref(conn);
		return;
	}

	buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(*cp));
	if (!buf) {
		LOG_DBG("Unable to allocate command buffer");
		goto reschedule;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);

	rc = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
	if (rc) {
		LOG_WRN("Read RSSI error (%d)", rc);
	} else {
		rp = (void *)rsp->data;
		LOG_DBG("%d RSSI: %d dBm", conn_idx, rp->rssi);
		conn_state->rssi = rp->rssi;
		net_buf_unref(rsp);
	}

reschedule:
	bt_conn_unref(conn);
	k_work_reschedule_for_queue(task_runner_work_q(), dwork,
				    K_MSEC(CONFIG_BT_CONN_AUTO_RSSI_INTERVAL_MS));
}

int8_t bt_conn_rssi(struct bt_conn *conn)
{
	return state[bt_conn_index(conn)].rssi;
}

#endif /* CONFIG_BT_CONN_AUTO_RSSI */

static struct bt_conn_cb conn_cb = {
	.connected = connected,
	.disconnected = disconnected,
};

static int infuse_bluetooth_gatt(void)
{
	ARG_UNUSED(state);

	for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
#ifdef CONFIG_BT_CONN_AUTO_RSSI
		k_work_init_delayable(&state[i].rssi_query, rssi_query_worker);
		state[i].rssi = 0;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */
#ifdef CONFIG_BT_GATT_CLIENT
		state[i].params = NULL;
#endif /* CONFIG_BT_GATT_CLIENT */
	}

	bt_conn_cb_register(&conn_cb);
	return 0;
}

SYS_INIT(infuse_bluetooth_gatt, POST_KERNEL, 0);
