/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <infuse/drivers/wifi/wifi_sim.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/network_scan.h>

NETWORK_SCAN_TASK(1, 0, NULL);
struct task_config config = NETWORK_SCAN_TASK(0, 1, NULL);

struct task_data data;
struct task_schedule schedule;
struct task_schedule_state state;

static struct tdf_network_scan_count logged_scan_count;
static struct tdf_wifi_ap_info logged_wifi_ap_info[64];
static uint8_t logged_wifi_ap_info_count;

static const struct wifi_scan_result wifi_results[] = {
	{
		.ssid = "HomeHub-2412",
		.ssid_length = 12,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.channel = 1,
		.security = WIFI_SECURITY_TYPE_PSK,
		.rssi = -38,
		.mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x50},
		.mac_length = 6,
	},
	{
		.ssid = "Office-Main",
		.ssid_length = 11,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.channel = 6,
		.security = WIFI_SECURITY_TYPE_SAE,
		.rssi = -47,
		.mac = {0x3c, 0x84, 0x6a, 0x12, 0x34, 0x60},
		.mac_length = 6,
	},
	{
		.ssid = "CafeGuest",
		.ssid_length = 9,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.channel = 11,
		.security = WIFI_SECURITY_TYPE_NONE,
		.rssi = -64,
		.mac = {0x8c, 0x85, 0x90, 0xab, 0xcd, 0x70},
		.mac_length = 6,
	},
	{
		.ssid = "CafeCore",
		.ssid_length = 8,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.channel = 11,
		.security = WIFI_SECURITY_TYPE_PSK,
		.rssi = -65,
		.mac = {0x8c, 0x85, 0x90, 0xab, 0xcd, 0x71},
		.mac_length = 6,
	},
	{
		.ssid = "HomeHub-5G",
		.ssid_length = 10,
		.band = WIFI_FREQ_BAND_5_GHZ,
		.channel = 36,
		.security = WIFI_SECURITY_TYPE_PSK,
		.rssi = -51,
		.mac = {0xa4, 0xcf, 0x12, 0x34, 0x56, 0x80},
		.mac_length = 6,
	},
	{
		.ssid = "Local-MAC",
		.ssid_length = 9,
		.band = WIFI_FREQ_BAND_5_GHZ,
		.channel = 149,
		.security = WIFI_SECURITY_TYPE_PSK,
		.rssi = -72,
		.mac = {0xF6, 0xd9, 0xe7, 0x65, 0x43, 0x90},
		.mac_length = 6,
	},
	{
		.ssid = "Lab-IoT",
		.ssid_length = 7,
		.band = WIFI_FREQ_BAND_5_GHZ,
		.channel = 161,
		.security = WIFI_SECURITY_TYPE_PSK,
		.rssi = -83,
		.mac = {0xb8, 0x27, 0xeb, 0x10, 0x20, 0xa0},
		.mac_length = 6,
	},
};

static void task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
}

static void expect_logging(uint8_t log_mask, bool scan_count_expected, bool wifi_ap_info_expected)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *pkt;
	int rc;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	if (log_mask == 0) {
		zassert_is_null(pkt);
		return;
	}

	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));

	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_NETWORK_SCAN_COUNT, &tdf);
	if (scan_count_expected) {
		zassert_equal(0, rc);
		memcpy(&logged_scan_count, tdf.data, sizeof(logged_scan_count));
	} else {
		zassert_equal(-ENOMEM, rc);
	}

	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_WIFI_AP_INFO, &tdf);
	if (wifi_ap_info_expected) {
		zassert_equal(0, rc);
		logged_wifi_ap_info_count = tdf.tdf_num;
		memcpy(&logged_wifi_ap_info, tdf.data, tdf.tdf_len * tdf.tdf_num);
	} else {
		zassert_equal(-ENOMEM, rc);
	}

	net_buf_unref(pkt);
}

ZTEST(task_network_scan, test_wifi_scan_not_requested)
{
	schedule = (struct task_schedule){
		.task_id = TASK_ID_NETWORK_SCAN,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.network_scan =
			{
				.flags = 0,
			},
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* Schedule the runner, no scan run, no logging */
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(0, false, false);

	/* Enable logging */
	schedule.task_logging[0].tdf_mask =
		TASK_NETWORK_SCAN_LOG_COUNT | TASK_NETWORK_SCAN_LOG_WIFI_AP;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;

	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, false);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(0, logged_scan_count.num_wifi);
}

ZTEST(task_network_scan, test_wifi_scan_no_results)
{
	schedule = (struct task_schedule){
		.task_id = TASK_ID_NETWORK_SCAN,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.network_scan =
			{
				.flags = TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS,
				.wifi.flags =
					TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_DUPLICATES |
					TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_LOCALLY_ADMINISTERED,
				.wifi.desired_aps = 3,
				.wifi.max_aps = 5,
			},
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* Schedule the runner, no scan results, no logging */
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(0, false, false);

	/* Enable logging, count only */
	schedule.task_logging[0].tdf_mask = TASK_NETWORK_SCAN_LOG_COUNT;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;

	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, false);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(0, logged_scan_count.num_wifi);

	/* Enable logging, WiFi APs, but no results */
	schedule.task_logging[0].tdf_mask = TASK_NETWORK_SCAN_LOG_WIFI_AP;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;

	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(0, false, false);
}

ZTEST(task_network_scan, test_wifi_scan_results)
{
	schedule = (struct task_schedule){
		.task_id = TASK_ID_NETWORK_SCAN,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.network_scan =
			{
				.flags = TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS,
				.wifi.flags =
					TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_DUPLICATES |
					TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_LOCALLY_ADMINISTERED,
				.wifi.desired_aps = 3,
				.wifi.max_aps = 25,
			},
	};

	/* Configure the results returned by the WiFi simulator */
	wifi_sim_scan_results_set(wifi_results, ARRAY_SIZE(wifi_results));

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* Schedule the runner, no scan results, no logging */
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(0, false, false);

	/* Enable logging, count only */
	schedule.task_logging[0].tdf_mask = TASK_NETWORK_SCAN_LOG_COUNT;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, false);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(ARRAY_SIZE(wifi_results), logged_scan_count.num_wifi);

	/* Logging APs */
	schedule.task_logging[0].tdf_mask = TASK_NETWORK_SCAN_LOG_WIFI_AP;
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, false, true);
	zassert_equal(ARRAY_SIZE(wifi_results), logged_wifi_ap_info_count);
	for (int i = 0; i < logged_wifi_ap_info_count; i++) {
		zassert_mem_equal(wifi_results[i].mac, logged_wifi_ap_info[i].bssid.val, 6);
		zassert_equal(wifi_results[i].rssi, logged_wifi_ap_info[i].rsrp);
		zassert_equal(wifi_results[i].channel, logged_wifi_ap_info[i].channel);
	}

	/* Uncapped results  */
	schedule.task_args.infuse.network_scan.wifi.max_aps = 0;
	schedule.task_logging[0].tdf_mask =
		TASK_NETWORK_SCAN_LOG_COUNT | TASK_NETWORK_SCAN_LOG_WIFI_AP;
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, true);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(ARRAY_SIZE(wifi_results), logged_scan_count.num_wifi);
	zassert_equal(ARRAY_SIZE(wifi_results), logged_wifi_ap_info_count);
	for (int i = 0; i < logged_wifi_ap_info_count; i++) {
		zassert_mem_equal(wifi_results[i].mac, logged_wifi_ap_info[i].bssid.val, 6);
		zassert_equal(wifi_results[i].rssi, logged_wifi_ap_info[i].rsrp);
		zassert_equal(wifi_results[i].channel, logged_wifi_ap_info[i].channel);
	}

	/* Limit the maximum reports */
	schedule.task_args.infuse.network_scan.wifi.max_aps = 4;
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, true);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(4, logged_scan_count.num_wifi);
	zassert_equal(4, logged_wifi_ap_info_count);
	for (int i = 0; i < logged_wifi_ap_info_count; i++) {
		zassert_mem_equal(wifi_results[i].mac, logged_wifi_ap_info[i].bssid.val, 6);
		zassert_equal(wifi_results[i].rssi, logged_wifi_ap_info[i].rsrp);
		zassert_equal(wifi_results[i].channel, logged_wifi_ap_info[i].channel);
	}

	/* Fewer results than requested */
	wifi_sim_scan_results_set(wifi_results, 2);
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, true);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(2, logged_scan_count.num_wifi);
	zassert_equal(2, logged_wifi_ap_info_count);
	for (int i = 0; i < logged_wifi_ap_info_count; i++) {
		zassert_mem_equal(wifi_results[i].mac, logged_wifi_ap_info[i].bssid.val, 6);
		zassert_equal(wifi_results[i].rssi, logged_wifi_ap_info[i].rsrp);
		zassert_equal(wifi_results[i].channel, logged_wifi_ap_info[i].channel);
	}
}

ZTEST(task_network_scan, test_wifi_scan_results_filtering)
{
	schedule = (struct task_schedule){
		.task_id = TASK_ID_NETWORK_SCAN,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.network_scan =
			{
				.flags = TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS,
				.wifi.desired_aps = 3,
				.wifi.max_aps = 25,
			},
		.task_logging = {{
			.tdf_mask = TASK_NETWORK_SCAN_LOG_COUNT | TASK_NETWORK_SCAN_LOG_WIFI_AP,
			.loggers = TDF_DATA_LOGGER_SERIAL,
		}}};

	/* Configure the results returned by the WiFi simulator */
	wifi_sim_scan_results_set(wifi_results, ARRAY_SIZE(wifi_results));

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* Run with locally administered and duplicate MACs filtered, we have one of each */
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, true);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(ARRAY_SIZE(wifi_results) - 2, logged_scan_count.num_wifi);
	/* Duplicate MAC is index 3, locally administered is index 5 */
	zassert_mem_equal(wifi_results[0].mac, logged_wifi_ap_info[0].bssid.val, 6);
	zassert_mem_equal(wifi_results[1].mac, logged_wifi_ap_info[1].bssid.val, 6);
	zassert_mem_equal(wifi_results[2].mac, logged_wifi_ap_info[2].bssid.val, 6);
	zassert_mem_equal(wifi_results[4].mac, logged_wifi_ap_info[3].bssid.val, 6);
	zassert_mem_equal(wifi_results[6].mac, logged_wifi_ap_info[4].bssid.val, 6);

	/* Only filter locally administered MACs */
	schedule.task_args.infuse.network_scan.wifi.flags =
		TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_DUPLICATES;
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, true);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(ARRAY_SIZE(wifi_results) - 1, logged_scan_count.num_wifi);
	zassert_mem_equal(wifi_results[0].mac, logged_wifi_ap_info[0].bssid.val, 6);
	zassert_mem_equal(wifi_results[1].mac, logged_wifi_ap_info[1].bssid.val, 6);
	zassert_mem_equal(wifi_results[2].mac, logged_wifi_ap_info[2].bssid.val, 6);
	zassert_mem_equal(wifi_results[3].mac, logged_wifi_ap_info[3].bssid.val, 6);
	zassert_mem_equal(wifi_results[4].mac, logged_wifi_ap_info[4].bssid.val, 6);
	zassert_mem_equal(wifi_results[6].mac, logged_wifi_ap_info[5].bssid.val, 6);

	/* Only filter duplicate MACs */
	schedule.task_args.infuse.network_scan.wifi.flags =
		TASK_NETWORK_SCAN_WIFI_FLAGS_INCLUDE_LOCALLY_ADMINISTERED;
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, true);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(ARRAY_SIZE(wifi_results) - 1, logged_scan_count.num_wifi);
	zassert_mem_equal(wifi_results[0].mac, logged_wifi_ap_info[0].bssid.val, 6);
	zassert_mem_equal(wifi_results[1].mac, logged_wifi_ap_info[1].bssid.val, 6);
	zassert_mem_equal(wifi_results[2].mac, logged_wifi_ap_info[2].bssid.val, 6);
	zassert_mem_equal(wifi_results[4].mac, logged_wifi_ap_info[3].bssid.val, 6);
	zassert_mem_equal(wifi_results[5].mac, logged_wifi_ap_info[4].bssid.val, 6);
	zassert_mem_equal(wifi_results[6].mac, logged_wifi_ap_info[5].bssid.val, 6);
}

ZTEST(task_network_scan, test_wifi_scan_mode)
{
	const struct wifi_scan_params *last_scan_params = wifi_sim_scan_params_get();

	schedule = (struct task_schedule){
		.task_id = TASK_ID_NETWORK_SCAN,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.network_scan =
			{
				.flags = TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS,
				.wifi.desired_aps = 3,
				.wifi.max_aps = 25,
			},
	};

	/* Configure the results returned by the WiFi simulator */
	wifi_sim_scan_results_set(wifi_results, ARRAY_SIZE(wifi_results));

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* Passive scanning by default, all bands */
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(0, false, false);
	zassert_equal(WIFI_SCAN_TYPE_PASSIVE, last_scan_params->scan_type);
	zassert_equal(BIT(WIFI_FREQ_BAND_2_4_GHZ) | BIT(WIFI_FREQ_BAND_5_GHZ),
		      last_scan_params->bands);

	/* Active when requested by schedule, still all bands */
	schedule.task_args.infuse.network_scan.wifi.flags =
		TASK_NETWORK_SCAN_WIFI_FLAGS_SCAN_ACTIVE;
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(0, false, false);
	zassert_equal(WIFI_SCAN_TYPE_ACTIVE, last_scan_params->scan_type);
	zassert_equal(BIT(WIFI_FREQ_BAND_2_4_GHZ) | BIT(WIFI_FREQ_BAND_5_GHZ),
		      last_scan_params->bands);
}

ZTEST(task_network_scan, test_wifi_progressive_scan)
{
	const struct wifi_scan_params *last_scan_params = wifi_sim_scan_params_get();

	schedule = (struct task_schedule){
		.task_id = TASK_ID_NETWORK_SCAN,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.network_scan =
			{
				.flags = TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS,
				.wifi.flags = TASK_NETWORK_SCAN_WIFI_FLAGS_SCAN_PROGRESSIVE,
				.wifi.desired_aps = 1,
				.wifi.max_aps = 25,
			},
	};

	/* Configure the results returned by the WiFi simulator */
	wifi_sim_scan_results_set(wifi_results, ARRAY_SIZE(wifi_results));

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* When only one network is desired, the progressive scan should find it on just the 2.4GHz
	 * band, never running a 5GHz scan.
	 */
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(0, false, false);
	zassert_equal(BIT(WIFI_FREQ_BAND_2_4_GHZ), last_scan_params->bands);

	/* When many networks are desired, the progressive scan should proceed to the 5GHz scan */
	schedule.task_args.infuse.network_scan.wifi.desired_aps = 10;
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(0, false, false);
	zassert_equal(BIT(WIFI_FREQ_BAND_5_GHZ), last_scan_params->bands);
}

ZTEST(task_network_scan, test_wifi_invalid_bssid)
{
	const struct wifi_scan_result wifi_results_bad_bssid[] = {
		{
			.ssid = "HomeHub-2412",
			.ssid_length = 12,
			.band = WIFI_FREQ_BAND_2_4_GHZ,
			.channel = 1,
			.security = WIFI_SECURITY_TYPE_PSK,
			.rssi = -38,
			.mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x50},
			.mac_length = 6,
		},
		{
			.ssid = "Office-Main",
			.ssid_length = 11,
			.band = WIFI_FREQ_BAND_2_4_GHZ,
			.channel = 6,
			.security = WIFI_SECURITY_TYPE_SAE,
			.rssi = -47,
			.mac = {0x3c, 0x84, 0x6a, 0x12, 0x34, 0x60},
			.mac_length = 0,
		},
		{
			.ssid = "CafeGuest",
			.ssid_length = 9,
			.band = WIFI_FREQ_BAND_2_4_GHZ,
			.channel = 11,
			.security = WIFI_SECURITY_TYPE_NONE,
			.rssi = -64,
			.mac = {0x8c, 0x85, 0x90, 0xab, 0xcd, 0x70},
			.mac_length = 6,
		},
	};

	schedule = (struct task_schedule){
		.task_id = TASK_ID_NETWORK_SCAN,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.network_scan =
			{
				.flags = TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS,
				.wifi.desired_aps = 2,
				.wifi.max_aps = 25,
			},
		.task_logging = {{
			.tdf_mask = TASK_NETWORK_SCAN_LOG_COUNT | TASK_NETWORK_SCAN_LOG_WIFI_AP,
			.loggers = TDF_DATA_LOGGER_SERIAL,
		}}};

	/* Configure the results returned by the WiFi simulator */
	wifi_sim_scan_results_set(wifi_results_bad_bssid, ARRAY_SIZE(wifi_results_bad_bssid));

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* The scan result without a valid BSSID should not be logged */
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	expect_logging(TDF_DATA_LOGGER_SERIAL, true, true);
	zassert_equal(0, logged_scan_count.num_lte);
	zassert_equal(2, logged_scan_count.num_wifi);
	/* Bad BSSID is index 1 */
	zassert_mem_equal(wifi_results_bad_bssid[0].mac, logged_wifi_ap_info[0].bssid.val, 6);
	zassert_mem_equal(wifi_results_bad_bssid[2].mac, logged_wifi_ap_info[1].bssid.val, 6);
}

static void logger_after(void *fixture)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt;

	/* Reset scan results */
	wifi_sim_scan_results_set(NULL, 0);

	/* Flush pending TDF data */
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	if (pkt) {
		net_buf_unref(pkt);
	}
}

ZTEST_SUITE(task_network_scan, NULL, NULL, NULL, logger_after, NULL);
