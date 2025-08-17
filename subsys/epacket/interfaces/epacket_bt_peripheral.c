/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>

#include <infuse/identifiers.h>
#include <infuse/bluetooth/gatt.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_bt_peripheral.h>
#include <infuse/security.h>
#include <infuse/task_runner/runner.h>
#include <infuse/time/epoch.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_bt_peripheral

#define ATT_HEADER_SIZE 3
#define PACKET_OVERHEAD (DT_INST_PROP(0, header_size) + DT_INST_PROP(0, footer_size))
#define TOTAL_OVERHEAD  (ATT_HEADER_SIZE + PACKET_OVERHEAD)

void epacket_bt_peripheral_logging_ccc_cfg_update(const struct bt_gatt_attr *attr, uint16_t value);

static ssize_t read_both(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset);
static ssize_t write_both(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags);

struct epacket_bt_peripheral_data {
	struct epacket_interface_common_data common_data;
	struct bt_conn_cb conn_cb;
	struct bt_gatt_cb gatt_cb;
	const struct device *interface;
	uint16_t last_notification;
	bool cmd_subscribed;
	bool data_subscribed;
};

/* Infuse-IoT Service Declaration */
BT_GATT_SERVICE_DEFINE(
	infuse_svc, BT_GATT_PRIMARY_SERVICE(INFUSE_SERVICE_UUID),
	BT_GATT_CHARACTERISTIC(INFUSE_SERVICE_UUID_COMMAND,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_both, write_both, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(INFUSE_SERVICE_UUID_DATA,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_both, write_both, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
#ifdef CONFIG_LOG_BACKEND_EPACKET_BT
	BT_GATT_CHARACTERISTIC(INFUSE_SERVICE_UUID_LOGGING, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ,
			       NULL, NULL, NULL),
	BT_GATT_CCC(epacket_bt_peripheral_logging_ccc_cfg_update,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
#endif /* CONFIG_LOG_BACKEND_EPACKET_BT */
);

#define CHRC_COMMAND 2
#define CCC_COMMAND  3
#define CHRC_DATA    5
#define CCC_DATA     6
#define CHRC_LOGGING 8

LOG_MODULE_REGISTER(epacket_bt_peripheral, CONFIG_EPACKET_BT_PERIPHERAL_LOG_LEVEL);

static void conn_mtu_query(struct bt_conn *conn, void *user_data)
{
	uint16_t *smallest_mtu = user_data;
	struct bt_conn_info info;
	int rc;

	/* Only care about connected objects */
	rc = bt_conn_get_info(conn, &info);
	if ((rc != 0) || (info.state != BT_CONN_STATE_CONNECTED)) {
		return;
	}

	/* Update state */
	*smallest_mtu = MIN(*smallest_mtu, bt_gatt_get_mtu(conn));
}

static void update_interface_state(void)
{
	const struct device *epacket_bt_peripheral = DEVICE_DT_GET(DT_DRV_INST(0));
	struct epacket_bt_peripheral_data *data = epacket_bt_peripheral->data;
	struct epacket_interface_cb *cb;
	uint16_t smallest_mtu = UINT16_MAX;
	uint16_t max_payload;

	/* Find the smallest MTU */
	bt_conn_foreach(BT_CONN_TYPE_LE, conn_mtu_query, &smallest_mtu);
	if ((smallest_mtu <= TOTAL_OVERHEAD) || (smallest_mtu == UINT16_MAX)) {
		/* Payload too small, treat as disconnected */
		smallest_mtu = 0;
	}

	if (smallest_mtu == 0) {
		if (data->last_notification == 0) {
			/* Don't notify disconnected again */
			return;
		}
		LOG_DBG("All disconnected");
		/* Interface is now disconnected */
		SYS_SLIST_FOR_EACH_CONTAINER(&data->common_data.callback_list, cb, node) {
			if (cb->interface_state) {
				cb->interface_state(0, cb->user_ctx);
			}
		}
		data->last_notification = 0;
		/* Reset any local throughput limits */
		epacket_rate_limit_reset();
		return;
	}

	/* Subtract overheads */
	max_payload = smallest_mtu - TOTAL_OVERHEAD;
	/* Ignore if value hasn't changed */
	if (data->last_notification == max_payload) {
		return;
	}

	LOG_DBG("Maximum payload: %d", max_payload);
	/* Interface is now connected */
	SYS_SLIST_FOR_EACH_CONTAINER(&data->common_data.callback_list, cb, node) {
		if (cb->interface_state) {
			cb->interface_state(max_payload, cb->user_ctx);
		}
	}
	data->last_notification = max_payload;
}

static ssize_t read_both(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	struct epacket_read_response response;

	if (offset > sizeof(response)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	len = MIN(len, sizeof(response) - offset);

	/* Populate values */
	infuse_security_cloud_public_key(response.cloud_public_key);
	infuse_security_device_public_key(response.device_public_key);
	response.network_id = infuse_security_network_key_identifier();

	/* Copy into output buffer */
	memcpy(buf, ((uint8_t *)&response) + offset, len);

	return len;
}

static ssize_t write_both(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags)
{
	const char *bt_addr_le_str(const bt_addr_le_t *addr);
	struct epacket_rx_metadata *meta;
	struct net_buf *rx_buffer;

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	rx_buffer = epacket_alloc_rx(K_MSEC(10));
	if (rx_buffer == NULL) {
		LOG_WRN("Buffer claim timeout");
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}
	if (len > net_buf_tailroom(rx_buffer)) {
		LOG_WRN("Insufficient space (%d > %d)", len, net_buf_tailroom(rx_buffer));
		net_buf_unref(rx_buffer);
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	LOG_DBG("%s: Wrote %d bytes", bt_addr_le_str(bt_conn_get_dst(conn)), len);

	/* Copy payload across */
	net_buf_add_mem(rx_buffer, buf, len);

	/* Save metadata */
	meta = net_buf_user_data(rx_buffer);
	meta->interface = DEVICE_DT_INST_GET(0);
	meta->interface_id = EPACKET_INTERFACE_BT_PERIPHERAL;
	meta->interface_address.bluetooth = *bt_conn_get_dst(conn);

#ifdef CONFIG_BT_CONN_AUTO_RSSI
	meta->rssi = bt_conn_rssi(conn);
#else
	meta->rssi = 0;
#endif /* CONFIG_BT_CONN_AUTO_RSSI */

	/* Hand off to ePacket core */
	epacket_raw_receive_handler(rx_buffer);

	/* Return the number of bytes handled (all of them) */
	return len;
}

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	/* Handle MTU change */
	update_interface_state();
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	/* Handle MTU change */
	update_interface_state();
}

const char *bt_addr_le_str(const bt_addr_le_t *addr);

static void epacket_bt_peripheral_send(const struct device *dev, struct net_buf *buf)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	const bt_addr_le_t *addr = &meta->interface_address.bluetooth;
	const struct bt_gatt_attr *attr;
	struct bt_conn *conn = NULL;
	int rc;

	/* Send to all, or specific connection */
	if (!bt_addr_le_eq(addr, BT_ADDR_LE_ANY)) {
		conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, addr);
		if (conn == NULL) {
			epacket_notify_tx_result(dev, buf, -ENOTCONN);
			net_buf_unref(buf);
			return;
		}
	}

	/* Encrypt the payload */
	if (epacket_bt_gatt_encrypt(buf, infuse_security_network_key_identifier()) < 0) {
		LOG_WRN("Failed to encrypt");
		epacket_notify_tx_result(dev, buf, -EIO);
		net_buf_unref(buf);
		return;
	}

	/* Send on different characteristic depending on data type.
	 * The purpose is to enable clients to send and receive commands without
	 * bogging down the connection with data that it isn't interested in.
	 */
	switch (meta->type) {
	case INFUSE_RPC_CMD:
	case INFUSE_RPC_DATA:
	case INFUSE_RPC_DATA_ACK:
	case INFUSE_RPC_RSP:
		attr = &infuse_svc.attrs[CHRC_COMMAND];
		break;
#ifdef CONFIG_LOG_BACKEND_EPACKET_BT
	case INFUSE_SERIAL_LOG:
		attr = &infuse_svc.attrs[CHRC_LOGGING];
		break;
#endif /* CONFIG_LOG_BACKEND_EPACKET_BT */
	default:
		attr = &infuse_svc.attrs[CHRC_DATA];
	}

	/* Forward the payload to all/specified connections */
	rc = bt_gatt_notify(conn, attr, buf->data, buf->len);
	if (rc == -ENOTCONN) {
		/* No-one connected is not an error condition */
		rc = 0;
	}
	epacket_notify_tx_result(dev, buf, rc);
	net_buf_unref(buf);
	if (conn) {
		/* Release connection object if obtained */
		bt_conn_unref(conn);
	}
}

static uint16_t epacket_bt_peripheral_max_packet(const struct device *dev)
{
	struct epacket_bt_peripheral_data *data = dev->data;

	if (data->last_notification == 0) {
		return 0;
	}
	return PACKET_OVERHEAD + data->last_notification;
}

static int epacket_bt_peripheral_init(const struct device *dev)
{
	struct epacket_bt_peripheral_data *data = dev->data;

	data->cmd_subscribed = false;
	data->data_subscribed = false;
	data->last_notification = 0;
	data->gatt_cb.att_mtu_updated = att_mtu_updated;
	data->conn_cb.disconnected = disconnected;
	bt_gatt_cb_register(&data->gatt_cb);
	bt_conn_cb_register(&data->conn_cb);

	__ASSERT(infuse_svc.attrs[CHRC_COMMAND].uuid->type == BT_UUID_TYPE_128,
		 "Characteristic order changed");
	__ASSERT(infuse_svc.attrs[CHRC_DATA].uuid->type == BT_UUID_TYPE_128,
		 "Characteristic order changed");
#ifdef CONFIG_LOG_BACKEND_EPACKET_BT
	__ASSERT(infuse_svc.attrs[CHRC_LOGGING].uuid->type == BT_UUID_TYPE_128,
		 "Characteristic order changed");
#endif /* CONFIG_LOG_BACKEND_EPACKET_BT */

	epacket_interface_common_init(dev);
	return 0;
}

static const struct epacket_interface_api bt_gatt_api = {
	.send = epacket_bt_peripheral_send,
	.max_packet_size = epacket_bt_peripheral_max_packet,
};

BUILD_ASSERT(244 == DT_INST_PROP(0, max_packet_size));
static struct epacket_bt_peripheral_data epacket_bt_peripheral_data_inst;
static const struct epacket_interface_common_config epacket_bt_peripheral_config = {
	.max_packet_size = EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(0)),
	.header_size = DT_INST_PROP(0, header_size),
	.footer_size = DT_INST_PROP(0, footer_size),
};
DEVICE_DT_DEFINE(DT_DRV_INST(0), epacket_bt_peripheral_init, NULL, &epacket_bt_peripheral_data_inst,
		 &epacket_bt_peripheral_config, POST_KERNEL, 0, &bt_gatt_api);
