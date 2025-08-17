/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

BUILD_ASSERT(CONFIG_BT_INFUSE_LEGACY_ADV_INTERVAL_MIN < CONFIG_BT_INFUSE_LEGACY_ADV_INTERVAL_MAX);

/* Bluetooth stack intervals are in 0.625 ms units */
#define INTERVAL_MIN (CONFIG_BT_INFUSE_LEGACY_ADV_INTERVAL_MIN / 0.625f)
#define INTERVAL_MAX (CONFIG_BT_INFUSE_LEGACY_ADV_INTERVAL_MAX / 0.625f)

static void legacy_connected(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_connected_info *info);
static void legacy_disconnected(struct bt_conn *conn, uint8_t reason);

#define BT_LE_ADV_CONN_SLOW BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, INTERVAL_MIN, INTERVAL_MAX, NULL)

static const struct bt_le_ext_adv_cb adv_cb = {
	.connected = legacy_connected,
};
BT_CONN_CB_DEFINE(conn_cb) = {
	.disconnected = legacy_disconnected,
};
static struct k_work_delayable start_advertising;
static struct bt_le_ext_adv *adv_set;
static struct bt_conn *legacy_conn;

LOG_MODULE_REGISTER(legacy_adv, LOG_LEVEL_INF);

static void legacy_connected(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_connected_info *info)
{
	/* Store connection associated with legacy advertising set */
	legacy_conn = info->conn;
	bt_conn_ref(legacy_conn);
}

static void legacy_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != legacy_conn) {
		return;
	}
	bt_conn_unref(conn);
	legacy_conn = NULL;
	/* Schedule work to restart advertising */
	k_work_reschedule(&start_advertising, K_MSEC(10));
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

static int bluetooth_name_update(void)
{
	struct bt_data ad[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
			sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	};
	__maybe_unused int rc;

#ifdef CONFIG_KV_STORE_KEY_DEVICE_NAME
	KV_KEY_TYPE_VAR(KV_KEY_DEVICE_NAME, 32) kv_name;

	/* Try to read out a name from the KV store */
	rc = kv_store_read(KV_KEY_DEVICE_NAME, &kv_name, sizeof(kv_name));
	if (rc > 0) {
		ad[0].data = kv_name.name.value;
		ad[0].data_len = kv_name.name.value_num - 1;
	}
#endif /* CONFIG_KV_STORE_KEY_DEVICE_NAME */

	LOG_INF("Using name '%s'", ad[0].data);
	/* Set advertising data to have complete local name set */
	return bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), NULL, 0);
}

#ifdef CONFIG_KV_STORE_KEY_DEVICE_NAME

static void legacy_adv_name_watcher(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	struct bt_data ad[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
			sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	};
	const struct kv_string *name;
	int rc;

	if (key != KV_KEY_DEVICE_NAME) {
		return;
	}

	if (data == NULL) {
		/* Revert to the default name */
	} else {
		/* Update name from written value */
		name = data;
		ad[0].data = name->value;
		ad[0].data_len = name->value_num - 1;
	}

	LOG_INF("Updating name to '%s'", ad[0].data);
	/* Update the name being advertised.
	 * This works regardless of whether the set is currently active.
	 */
	rc = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), NULL, 0);
	if (rc) {
		LOG_WRN("Failed to update AD name (%d)", rc);
	}
}

#endif /* CONFIG_KV_STORE_KEY_DEVICE_NAME */

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

	/* Update the Bluetooth device name */
	rc = bluetooth_name_update();
	if (rc) {
		return rc;
	}

#ifdef CONFIG_KV_STORE_KEY_DEVICE_NAME
	static struct kv_store_cb name_watcher = {
		.value_changed = legacy_adv_name_watcher,
	};

	/* Watch for changes to the device name */
	kv_store_register_callback(&name_watcher);
#endif /* CONFIG_KV_STORE_KEY_DEVICE_NAME */

	/* Start advertising */
	return k_work_reschedule(&start_advertising, K_NO_WAIT);
}
