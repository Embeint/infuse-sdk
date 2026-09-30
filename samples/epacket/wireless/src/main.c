/**
 * @copyright 2026 Embeint Holdings Pty Ltd
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <inttypes.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_bt.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/identifiers.h>
#include <infuse/tdf/definitions.h>
#include <infuse/version.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct device *led = DEVICE_DT_GET_OR_NULL(DT_ALIAS(status_led));

/* The ePacket advertising interface owns advertising when enabled. */
#ifndef CONFIG_EPACKET_INTERFACE_BT_ADV
static const struct bt_data advertising[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(INFUSE_BT_SERVICE_UUID_VAL)),
};

static void advertising_start(struct k_work *work)
{
	const char *name = bt_get_name();
	const struct bt_data scan_response[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
	};
	int rc;

	ARG_UNUSED(work);
	rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising, ARRAY_SIZE(advertising),
			     scan_response, ARRAY_SIZE(scan_response));
	if (rc && rc != -EALREADY) {
		LOG_ERR("Unable to start BLE advertising (%d)", rc);
	}
}

static K_WORK_DEFINE(advertising_work, advertising_start);

static void connection_recycled(void)
{
	k_work_submit(&advertising_work);
}

BT_CONN_CB_DEFINE(sample_conn_cb) = {
	.recycled = connection_recycled,
};

#endif /* CONFIG_EPACKET_INTERFACE_BT_ADV */

static struct net_mgmt_event_callback wifi_scan_cb;
static uint32_t wifi_scan_results;

static void wifi_scan_event(struct net_mgmt_event_callback *cb, uint64_t event,
			    struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (event == NET_EVENT_WIFI_SCAN_RESULT) {
		wifi_scan_results++;
	} else if (event == NET_EVENT_WIFI_SCAN_DONE) {
		const struct wifi_status *status = cb->info;

		LOG_INF("Wi-Fi scan: %u access points, status %d", wifi_scan_results,
			status ? status->status : -EIO);
	}
}

static void wifi_scan_if_unconfigured(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	int rc;

	KV_KEY_TYPE_VAR(KV_KEY_WIFI_SSID, WIFI_SSID_MAX_LEN) ssid;

	if (iface == NULL || kv_store_read(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid)) > 0) {
		return;
	}
	net_mgmt_init_event_callback(&wifi_scan_cb, wifi_scan_event,
				     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
	net_mgmt_add_event_callback(&wifi_scan_cb);
	rc = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (rc) {
		LOG_WRN("Unable to start Wi-Fi scan (%d)", rc);
	}
}

int main(void)
{
	const struct device *serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));
	struct infuse_version version = application_version_get();
	struct kv_reboots reboots = {0};
	struct tdf_announce_v2 announce = {0};
	uint64_t id = infuse_device_id();
	const struct device *bt_periph = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_peripheral));
	const struct device *udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));
	const uint32_t loggers =
		TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_BT_PERIPHERAL | TDF_DATA_LOGGER_UDP |
		(IS_ENABLED(CONFIG_EPACKET_INTERFACE_BT_ADV) ? TDF_DATA_LOGGER_BT_ADV : 0);
	bool led_ready = false;
	bool led_state = false;
	int rc;

	if (!device_is_ready(serial)) {
		LOG_ERR("USB ePacket interface is not ready");
		return -ENODEV;
	}
	led_ready = led != NULL && device_is_ready(led);
	if (led != NULL && !led_ready) {
		LOG_WRN("Onboard LED unavailable; USB remains active");
	}

	(void)KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	rc = epacket_receive(serial, K_FOREVER);
	if (rc < 0) {
		LOG_ERR("Unable to enable ePacket reception (%d)", rc);
		return rc;
	}

	if (!device_is_ready(bt_periph) || !device_is_ready(udp)) {
		LOG_ERR("Wireless ePacket interface is not ready");
		return -ENODEV;
	}
#ifndef CONFIG_EPACKET_INTERFACE_BT_ADV
	k_work_submit(&advertising_work);
#endif

	wifi_scan_if_unconfigured();

	announce.application = CONFIG_INFUSE_APPLICATION_ID;
	announce.reboots = reboots.count;
	announce.version.major = version.major;
	announce.version.minor = version.minor;
	announce.version.revision = version.revision;
	announce.version.build_num = version.build_num;
	announce.board_crc = crc16_ccitt(0, CONFIG_BOARD_TARGET, strlen(CONFIG_BOARD_TARGET));

	for (;;) {
		if (led_ready) {
			led_state = !led_state;
			rc = led_state ? led_on(led, 0) : led_off(led, 0);
			if (rc < 0) {
				LOG_ERR("Onboard LED update failed (%d)", rc);
				led_ready = false;
			}
		}
		announce.uptime = k_uptime_seconds();
		if ((announce.uptime % 5) == 0) {
			LOG_INF("Device %016" PRIx64 ": uptime %u s, reboots %u", id,
				announce.uptime, reboots.count);
			TDF_DATA_LOGGER_LOG(loggers, TDF_ANNOUNCE_V2, 0, &announce);
			tdf_data_logger_flush(loggers);
		}
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
