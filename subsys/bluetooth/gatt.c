/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/init.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/slist.h>
#include <zephyr/kernel.h>

#include <infuse/work_q.h>
#include <infuse/bluetooth/gatt.h>
#include <infuse/time/epoch.h>
#include <infuse/task_runner/runner.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/states.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_BT_CONNECTIONS
#include <memfault/metrics/metrics.h>
#endif /* CONFIG_MEMFAULT_INFUSE_METRICS_BT_CONNECTIONS */

static struct bt_gatt_state {
#ifdef CONFIG_BT_CONN_AUTO_RSSI
	struct k_work_delayable rssi_query;
	uint8_t rssi_log;
	int8_t rssi;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */
#ifdef CONFIG_BT_GATT_CLIENT
	const struct bt_conn_auto_setup_cb *cb;
	struct bt_conn_auto_discovery *discovery;
	uint8_t preferred_phy;
	bool connect_cb_run;
#endif /* CONFIG_BT_GATT_CLIENT */
} state[CONFIG_BT_MAX_CONN];

struct bt_disconnect_node {
	sys_snode_t node;
	struct bt_conn *conn;
	struct k_sem sem;
};
static sys_slist_t disconnect_list;
static struct k_spinlock disconnect_lock;

LOG_MODULE_REGISTER(infuse_gatt, LOG_LEVEL_INF);

const char *bt_addr_le_str(const bt_addr_le_t *addr);

#ifdef CONFIG_BT_GATT_CLIENT

static const struct bt_uuid_16 ccc_uuid = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);
static const struct bt_uuid_16 db_hash_uuid = BT_UUID_INIT_16(BT_UUID_GATT_DB_HASH_VAL);
static struct bt_gatt_exchange_params mtu_exchange_params;
static struct bt_gatt_read_params db_read_params;

void bt_conn_le_auto_setup(struct bt_conn *conn, struct bt_conn_auto_discovery *discovery,
			   const struct bt_conn_auto_setup_cb *callbacks, uint8_t preferred_phy)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	s->connect_cb_run = false;
	s->discovery = discovery;
	s->cb = callbacks;
	s->preferred_phy = preferred_phy;
}

static void connection_done(struct bt_conn *conn)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	/* We will call `conn_setup_cb` */
	s->connect_cb_run = true;

	/* Run user callback */
	s->cb->conn_setup_cb(conn, 0, s->cb->user_data);
}

static void connection_error(struct bt_conn *conn, int err)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	LOG_ERR("Connection setup failed (%d)", err);

	/* We will call `conn_setup_cb` */
	s->connect_cb_run = true;

	/* Run user callback */
	s->cb->conn_setup_cb(conn, err, s->cb->user_data);

	/* Clear state */
	s->cb = NULL;
}

static void descriptor_discovery(struct bt_conn *conn);

static uint8_t ccc_discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       struct bt_gatt_discover_params *params, int err)
{
	if (err != 0) {
		connection_error(conn, err);
		return BT_GATT_ITER_STOP;
	}
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
	for (int i = 0; i < s->discovery->num_characteristics; i++) {
		remote_char = &s->discovery->remote_info[i];
		if (remote_char->attr_start_handle == 0) {
			/* Characteristic was not found on remote */
			continue;
		}
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
	bool any_found = false;
	int rc;

	/* Find characteristic without CCC yet found */
	for (int i = 0; i < s->discovery->num_characteristics; i++) {
		remote_char = &s->discovery->remote_info[i];

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
			connection_error(conn, rc);
		}
		return;
	}

	LOG_INF("Characteristic discovery complete");
	for (int i = 0; i < s->discovery->num_characteristics; i++) {
		remote_char = &s->discovery->remote_info[i];
		if (remote_char->attr_start_handle) {
			any_found = true;
		}
		LOG_INF("\t%d: Range (%5d - %5d) Value %d CCC %d", i,
			remote_char->attr_start_handle, remote_char->attr_end_handle,
			remote_char->value_handle, remote_char->ccc_handle);
	}

	/* Overwrite the cache if we found any of the requested characteristics */
	if (any_found && s->discovery->cache) {
		K_SPINLOCK(&s->discovery->cache->lock) {
			/* Copy the DB hash */
			memcpy(s->discovery->cache->db_hash, s->discovery->db_hash_pending,
			       sizeof(s->discovery->db_hash_pending));
			/* Copy the characteristics */
			memcpy(s->discovery->cache->remote_info, s->discovery->remote_info,
			       s->discovery->num_characteristics *
				       sizeof(struct bt_gatt_remote_char));
		}
	}

	/* Connection has been setup and discovered */
	connection_done(conn);
}

static uint8_t char_discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *params, int err)
{
	if (err != 0) {
		connection_error(conn, err);
		return BT_GATT_ITER_STOP;
	}
	if (!attr) {
		descriptor_discovery(conn);
		return BT_GATT_ITER_STOP;
	}

	/* Extract characteristic information from the attribute */
	struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];
	struct bt_gatt_remote_char *remote_char;
	const struct bt_uuid *uuid;

	LOG_DBG("ATTR Handle %d Value Handle %d Properties %02X", attr->handle, chrc->value_handle,
		chrc->properties);

	/* Determine if this characteristic is one we are looking for */
	for (int i = 0; i < s->discovery->num_characteristics; i++) {
		uuid = s->discovery->characteristics[i];
		remote_char = &s->discovery->remote_info[i];

		if (remote_char->attr_start_handle != 0x0000) {
			/* Update the previous characteristic end handle if appropriate */
			if (remote_char->attr_end_handle == BT_ATT_LAST_ATTRIBUTE_HANDLE) {
				remote_char->attr_end_handle = attr->handle - 1;
			}
			/* Already found */
			continue;
		}
		if (bt_uuid_cmp(uuid, chrc->uuid) == 0) {
			remote_char->properties = chrc->properties;
			remote_char->attr_start_handle = attr->handle;
			remote_char->value_handle = chrc->value_handle;
			break;
		}
	}

	/* Are we still looking for any characteristics? */
	for (int i = 0; i < s->discovery->num_characteristics; i++) {
		remote_char = &s->discovery->remote_info[i];
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
	for (int i = 0; i < s->discovery->num_characteristics; i++) {
		s->discovery->remote_info[i].attr_start_handle = 0x0000;
		s->discovery->remote_info[i].attr_end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		s->discovery->remote_info[i].value_handle = 0x0000;
		s->discovery->remote_info[i].ccc_handle = 0x0000;
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
		connection_error(conn, rc);
	}
}

uint8_t gatt_db_hash_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
			const void *data, uint16_t length)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];
	bool done = false;

	if ((err) || (length != sizeof(s->discovery->cache->db_hash))) {
		LOG_WRN("Failed to read DB hash (%d)", err);
	} else {
		K_SPINLOCK(&s->discovery->cache->lock) {
			/* If DB hash matches cached value, we can skip discovery */
			if (memcmp(s->discovery->cache->db_hash, data, length) == 0) {
				LOG_INF("Characteristic handles from %s", "cache");
				/* Copy from cache into parameters */
				memcpy(s->discovery->remote_info, s->discovery->cache->remote_info,
				       s->discovery->num_characteristics *
					       sizeof(struct bt_gatt_remote_char));
				done = true;
			}
		}
		if (done) {
			connection_done(conn);
			return BT_GATT_ITER_STOP;
		}

		LOG_INF("Characteristic handles from %s", "discovery");
		/* Cache the database hash while performing discovery */
		memcpy(s->discovery->db_hash_pending, data, length);
	}

	/* Start characteristic discovery */
	characteristic_discovery(conn);
	return BT_GATT_ITER_STOP;
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];

	if (err) {
		connection_error(conn, err);
		return;
	}

	LOG_DBG("MTU exchange %s (%u)", err == 0U ? "successful" : "failed", bt_gatt_get_mtu(conn));

	if (s->discovery == NULL || s->discovery->num_characteristics == 0) {
		/* No characteristic discovery to do, connection complete */
		connection_done(conn);
		return;
	}

	if (s->discovery->cache == NULL) {
		/* No cache, skip to discovery */
		characteristic_discovery(conn);
		return;
	}

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

#ifdef CONFIG_BT_USER_PHY_UPDATE

static void phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	struct bt_conn_info info;
	int rc;

	LOG_DBG("PHY updated: %02X %02X", param->rx_phy, param->tx_phy);

	/* This can only possibly fail if the connection type is not BT_CONN_TYPE_LE */
	rc = bt_conn_get_info(conn, &info);
	__ASSERT_NO_MSG(rc == 0);
	if (info.role != BT_CONN_ROLE_CENTRAL) {
		/* We didn't initiate this connection */
		return;
	}

	/* Continue setting up connection with MTU exchange */
	mtu_exchange_params.func = mtu_exchange_cb;
	rc = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
	if (rc < 0) {
		connection_error(conn, rc);
	}
}

/* Returns True if this function handles next step of connection setup */
static bool central_phy_request(struct bt_conn *conn)
{
	struct bt_gatt_state *s = &state[bt_conn_index(conn)];
	struct bt_conn_info info;
	int rc;

	if (s->preferred_phy == BT_GAP_LE_PHY_NONE) {
		return false;
	}

	/* Get current PHY */
	rc = bt_conn_get_info(conn, &info);
	if (rc < 0) {
		connection_error(conn, rc);
		return true;
	}

	if (info.le.phy->rx_phy & s->preferred_phy) {
		/* Preferred PHY already used */
		return false;
	}

	/* No overlap between preferred and current PHY */
	struct bt_conn_le_phy_param phy_params = {
		.options = BT_CONN_LE_PHY_OPT_NONE,
		.pref_rx_phy = s->preferred_phy,
		.pref_tx_phy = s->preferred_phy,
	};

	rc = bt_conn_le_phy_update(conn, &phy_params);
	if (rc == 0) {
		/* Waiting for PHY update callback */
	} else if (rc == -EIO) {
		/* Requested PHY not supported, fallthrough to MTU exchange */
		LOG_WRN("Unsupported PHY request %02X", s->preferred_phy);
	} else {
		/* Some other failure */
		connection_error(conn, rc);
	}
	return rc != -EIO;
}

#else

static bool central_phy_request(struct bt_conn *conn)
{
	ARG_UNUSED(conn);

	return false;
}

#endif /* CONFIG_BT_USER_PHY_UPDATE */

static void central_conn_setup(struct bt_conn *conn)
{
	int rc;

	/* First action, request PHY update if preferred PHY set */
	if (central_phy_request(conn)) {
		/* MTU exchange triggered by PHY update */
		return;
	}

	/* First action if no PHY update, request MTU update */
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
	struct bt_gatt_state *s __maybe_unused = &state[bt_conn_index(conn)];
	int rc __maybe_unused;

#ifdef CONFIG_BT_GATT_CLIENT
	struct bt_conn_info info;

	rc = bt_conn_get_info(conn, &info);
	/* This can only possibly fail if the connection type is not BT_CONN_TYPE_LE */
	__ASSERT_NO_MSG(rc == 0);

#ifdef CONFIG_MEMFAULT_INFUSE_METRICS_BT_CONNECTIONS
	if (info.role == BT_CONN_ROLE_CENTRAL) {
		/* Connection we initiated */
		if (err == BT_HCI_ERR_SUCCESS) {
			(void)MEMFAULT_METRIC_ADD(epacket_bt_central_conn_success, 1);
		} else {
			(void)MEMFAULT_METRIC_ADD(epacket_bt_central_conn_failed, 1);
		}
	}
#endif /* CONFIG_MEMFAULT_INFUSE_METRICS_BT_CONNECTIONS */
#endif /* CONFIG_BT_GATT_CLIENT */

	if (err == BT_HCI_ERR_SUCCESS) {
		LOG_INF("Connected to %s", bt_addr_le_str(dst));
	} else {
		LOG_WRN("Connection to %s failed (error 0x%02X)", bt_addr_le_str(dst), err);
#ifdef CONFIG_BT_GATT_CLIENT
		if (s->cb) {
			connection_error(conn, err);
		}
#endif /* CONFIG_BT_GATT_CLIENT */
		return;
	}

#ifdef CONFIG_BT_GATT_CLIENT

	/* Only handle connections initiated through bt_conn_le_auto_setup */
	if (s->cb != NULL) {
		if (info.role == BT_CONN_ROLE_CENTRAL) {
			central_conn_setup(conn);
		}
	}
#endif /* CONFIG_BT_GATT_CLIENT */

#ifdef CONFIG_BT_CONN_AUTO_RSSI
	/* Small delay to give controller a chance to finish setup */
	infuse_work_reschedule(&state[bt_conn_index(conn)].rssi_query, K_MSEC(50));
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
	s->rssi_log = 0;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */
#ifdef CONFIG_BT_GATT_CLIENT
	if (s->cb) {
		/* Have we run `conn_setup_cb` yet?
		 * We can expect this to happen with a PHY update procedure, which
		 * doesn't have a result callback that can fail, and hence there is
		 * nowhere to run `connection_error` from.
		 */
		if (!s->connect_cb_run) {
			s->cb->conn_setup_cb(conn, reason, s->cb->user_data);
		} else if (s->cb->conn_terminated_cb) {
			s->cb->conn_terminated_cb(conn, reason, s->cb->user_data);
		}
		s->cb = NULL;
	}
#endif /* CONFIG_BT_GATT_CLIENT */

	LOG_INF("Disconnected from %s (reason 0x%02X)", bt_addr_le_str(dst), reason);

	struct bt_disconnect_node *node, *nodes;

	K_SPINLOCK(&disconnect_lock) {
		SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&disconnect_list, node, nodes, node) {
			if (conn == node->conn) {
				k_sem_give(&node->sem);
			}
		}
	}
}

int bt_conn_disconnect_sync(struct bt_conn *conn)
{
	struct bt_disconnect_node node = {
		.conn = conn,
	};
	int rc;

	/* Initialise node for list */
	k_sem_init(&node.sem, 0, 1);
	K_SPINLOCK(&disconnect_lock) {
		sys_slist_append(&disconnect_list, &node.node);
	}

	/* Trigger the disconnection */
	rc = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (rc < 0) {
		goto end;
	}

	/* Wait for the connection to terminate */
	rc = k_sem_take(&node.sem, K_SECONDS(5));
end:
	K_SPINLOCK(&disconnect_lock) {
		sys_slist_find_and_remove(&disconnect_list, &node.node);
	}
	return rc;
}

int bt_conn_disconnect_wait(struct bt_conn *conn, k_timeout_t timeout)
{
	struct bt_disconnect_node node = {
		.conn = conn,
	};
	int rc;

	/* Initialise node for list */
	k_sem_init(&node.sem, 0, 1);
	K_SPINLOCK(&disconnect_lock) {
		sys_slist_append(&disconnect_list, &node.node);
	}

	/* Wait for the connection to terminate */
	rc = k_sem_take(&node.sem, timeout);
	K_SPINLOCK(&disconnect_lock) {
		sys_slist_find_and_remove(&disconnect_list, &node.node);
	}
	return rc;
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

#ifdef CONFIG_INFUSE_APPLICATION_STATES
	if (infuse_state_get(INFUSE_STATE_REBOOTING)) {
		/* Device is about to reboot, don't create more work */
		return;
	}
#endif /* CONFIG_INFUSE_APPLICATION_STATES */

	if (conn == NULL) {
		/* Work was not cancelled in time */
		LOG_DBG("Invalid connection");
		return;
	}
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

#ifdef CONFIG_TDF_DATA_LOGGER
		if (conn_state->rssi_log) {
			const bt_addr_le_t *dst = bt_conn_get_dst(conn);
			struct tdf_bluetooth_rssi tdf;

			tdf_bt_addr_le_from_stack(dst, &tdf.address);
			tdf.rssi = rp->rssi;

			TDF_DATA_LOGGER_LOG(conn_state->rssi_log, TDF_BLUETOOTH_RSSI,
					    epoch_time_now(), &tdf);
		}
#endif /* CONFIG_TDF_DATA_LOGGER */
	}

reschedule:
	bt_conn_unref(conn);
	infuse_work_reschedule(dwork, K_MSEC(CONFIG_BT_CONN_AUTO_RSSI_INTERVAL_MS));
}

int8_t bt_conn_rssi(struct bt_conn *conn)
{
	return state[bt_conn_index(conn)].rssi;
}

#ifdef CONFIG_TDF_DATA_LOGGER

void bt_conn_rssi_log(struct bt_conn *conn, uint8_t tdf_loggers)
{
	state[bt_conn_index(conn)].rssi_log = tdf_loggers;
}

#endif /* CONFIG_TDF_DATA_LOGGER */
#endif /* CONFIG_BT_CONN_AUTO_RSSI */

static struct bt_conn_cb conn_cb;

static int infuse_bluetooth_gatt(void)
{
	ARG_UNUSED(state);

	for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
#ifdef CONFIG_BT_CONN_AUTO_RSSI
		k_work_init_delayable(&state[i].rssi_query, rssi_query_worker);
		state[i].rssi_log = 0;
		state[i].rssi = 0;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */
#ifdef CONFIG_BT_GATT_CLIENT
		state[i].discovery = NULL;
		state[i].cb = NULL;
#endif /* CONFIG_BT_GATT_CLIENT */
	}

	/* Callback registration */
	conn_cb.connected = connected;
	conn_cb.disconnected = disconnected;
#ifdef CONFIG_BT_USER_PHY_UPDATE
	conn_cb.le_phy_updated = phy_updated;
#endif /* CONFIG_BT_USER_PHY_UPDATE */
	bt_conn_cb_register(&conn_cb);
	return 0;
}

SYS_INIT(infuse_bluetooth_gatt, POST_KERNEL, 0);
