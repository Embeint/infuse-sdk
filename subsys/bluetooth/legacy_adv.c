/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

#include <infuse/bluetooth/legacy_adv.h>

BUILD_ASSERT(CONFIG_BT_INFUSE_LEGACY_ADV_INTERVAL_MIN < CONFIG_BT_INFUSE_LEGACY_ADV_INTERVAL_MAX);

/* Bluetooth stack intervals are in 0.625 ms units */
#define INTERVAL_MIN (CONFIG_BT_INFUSE_LEGACY_ADV_INTERVAL_MIN / 0.625f)
#define INTERVAL_MAX (CONFIG_BT_INFUSE_LEGACY_ADV_INTERVAL_MAX / 0.625f)

static void legacy_connected(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_connected_info *info);
static void legacy_recycled(const void *conn);

#define BT_LE_ADV_CONN_SLOW BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, INTERVAL_MIN, INTERVAL_MAX, NULL)

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
static const struct bt_le_ext_adv_cb adv_cb = {
	.connected = legacy_connected,
};
BT_CONN_CB_DEFINE(conn_cb) = {
	.recycled = legacy_recycled,
};
static struct k_work_delayable start_advertising;
static struct bt_le_ext_adv *adv_set;
static struct bt_conn *legacy_conn;

LOG_MODULE_REGISTER(legacy_adv, LOG_LEVEL_INF);

static void legacy_connected(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_connected_info *info)
{
	/* Store connection associated with legacy advertising set */
	legacy_conn = info->conn;
}

static void legacy_recycled(const void *conn)
{
	if (conn != legacy_conn) {
		return;
	}
	legacy_conn = NULL;
	/* Schedule work to restart advertising */
	k_work_reschedule(&start_advertising, K_NO_WAIT);
}

static void start_advertising_work(struct k_work *work)
{
	int rc;

	rc = bt_le_ext_adv_start(adv_set, BT_LE_EXT_ADV_START_DEFAULT);
	if (rc) {
		LOG_ERR("Failed to resume legacy advertising set");
		/* Try again in 10 seconds*/
		k_work_reschedule(&start_advertising, K_SECONDS(10));
	}
}

int bluetooth_legacy_advertising_run(void)
{
	int rc;

	/* Initialise work */
	k_work_init_delayable(&start_advertising, start_advertising_work);

	/* Create a connectable advertising set */
	rc = bt_le_ext_adv_create(BT_LE_ADV_CONN_SLOW, &adv_cb, &adv_set);
	if (rc) {
		return rc;
	}

	/* Set advertising data to have complete local name set */
	rc = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), NULL, 0);
	if (rc) {
		return rc;
	}

	/* Start advertising */
	return k_work_reschedule(&start_advertising, K_NO_WAIT);
}
