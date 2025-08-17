/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/bluetooth/conn.h>

#include <infuse/work_q.h>
#include <infuse/auto/bluetooth_conn_log.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>
#include <infuse/data_logger/high_level/tdf.h>

static struct tdf_bluetooth_connection tdf_bt_conn;
static K_SEM_DEFINE(tdf_access, 1, 1);
static struct k_work logger_work;
static uint8_t loggers;
static uint8_t log_flags;

static void log_do(struct k_work *work)
{
	/* Log with time if not flushing */
	uint64_t epoch_time = log_flags & AUTO_BT_CONN_LOG_EVENTS_FLUSH ? 0 : epoch_time_now();

	TDF_DATA_LOGGER_LOG(loggers, TDF_BLUETOOTH_CONNECTION, epoch_time, &tdf_bt_conn);
	if (log_flags & AUTO_BT_CONN_LOG_EVENTS_FLUSH) {
		tdf_data_logger_flush(loggers);
	}

	k_sem_give(&tdf_access);
}

static void log_construct(const bt_addr_le_t *remote, uint8_t state)
{
	/* This may lead to lost events, but is preferable to blocking the BT receive workqueue */
	if (k_sem_take(&tdf_access, K_NO_WAIT) != 0) {
		return;
	}
	tdf_bt_addr_le_from_stack(remote, &tdf_bt_conn.address);
	tdf_bt_conn.connected = state;
	infuse_work_submit(&logger_work);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	if (err != 0) {
		return;
	}
	log_construct(dst, 1);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	log_construct(dst, 0);
}

static struct bt_conn_cb conn_cb;

void auto_bluetooth_conn_log_configure(uint8_t tdf_logger_mask, uint8_t flags)
{
	k_work_init(&logger_work, log_do);
	loggers = tdf_logger_mask;
	log_flags = flags;

	/* Callback registration */
	conn_cb.connected = connected;
	conn_cb.disconnected = disconnected;
	bt_conn_cb_register(&conn_cb);
}
