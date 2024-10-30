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

#include <infuse/task_runner/runner.h>

#ifdef CONFIG_BT_CONN_AUTO_RSSI
static struct bt_gatt_state {
	struct k_work_delayable rssi_query;
	int8_t rssi;
} state[CONFIG_BT_MAX_CONN];
#endif /* CONFIG_BT_CONN_AUTO_RSSI */

LOG_MODULE_REGISTER(infuse_gatt, LOG_LEVEL_INF);

const char *bt_addr_le_str(const bt_addr_le_t *addr);

static void connected(struct bt_conn *conn, uint8_t err)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	LOG_INF("Connected to %s (error 0x%02X)", bt_addr_le_str(dst), err);

#ifdef CONFIG_BT_CONN_AUTO_RSSI
	/* Small delay to give controller a chance to finish setup */
	k_work_reschedule_for_queue(task_runner_work_q(), &state[bt_conn_index(conn)].rssi_query,
				    K_MSEC(10));
#endif /* CONFIG_BT_CONN_AUTO_RSSI */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

#ifdef CONFIG_BT_CONN_AUTO_RSSI
	k_work_cancel_delayable(&state[bt_conn_index(conn)].rssi_query);
	state[bt_conn_index(conn)].rssi = 0;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */

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
#ifdef CONFIG_BT_CONN_AUTO_RSSI
	for (int i = 0; i < ARRAY_SIZE(state); i++) {
		k_work_init_delayable(&state[i].rssi_query, rssi_query_worker);
		state[i].rssi = 0;
	}
#endif /* CONFIG_BT_CONN_AUTO_RSSI */

	bt_conn_cb_register(&conn_cb);
	return 0;
}

SYS_INIT(infuse_bluetooth_gatt, POST_KERNEL, 0);
