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
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(infuse_gatt, LOG_LEVEL_INF);

const char *bt_addr_le_str(const bt_addr_le_t *addr);

static void connected(struct bt_conn *conn, uint8_t err)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	LOG_INF("Connected to %s (error 0x%02X)", bt_addr_le_str(dst), err);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	LOG_INF("Disconnected from %s (reason 0x%02X)", bt_addr_le_str(dst), reason);
}

static struct bt_conn_cb conn_cb = {
	.connected = connected,
	.disconnected = disconnected,
};

static int infuse_bluetooth_gatt(void)
{
	bt_conn_cb_register(&conn_cb);
	return 0;
}

SYS_INIT(infuse_bluetooth_gatt, POST_KERNEL, 0);
