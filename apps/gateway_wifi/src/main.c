/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#ifdef CONFIG_SOC_NRF5340_CPUAPP
#include <nrfx_clock.h>
#endif /* CONFIG_SOC_NRF5340_CPUAPP */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/auto/bluetooth_conn_log.h>
#include <infuse/auto/wifi_conn_log.h>
#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_udp.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 5,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_UDP,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_NET_CONN,
			},
	},
	{
		.task_id = TASK_ID_TDF_LOGGER_ALT1,
		.validity = TASK_VALID_PERMANENTLY_RUNS,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV,
				.logging_period_ms = 4500,
				.random_delay_ms = 1000,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_NET_CONN,
			},
	},
};

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL),
			 (TDF_LOGGER_ALT1_TASK, NULL));

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);

GATEWAY_HANDLER_DEFINE(udp_backhaul_handler, DEVICE_DT_GET(DT_NODELABEL(epacket_udp)));

int main(void)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	const struct device *epacket_bt_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));
	const struct device *epacket_udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));

#ifdef CONFIG_SOC_NRF5340_CPUAPP
	int err;

	/* For optimal performance, the CPU frequency needs to be set to 128 MHz */
	err = nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
	err -= NRFX_ERROR_BASE_NUM;
	if (err != 0) {
		LOG_WRN("Failed to set 128 MHz: %d", err);
	}
#endif /* CONFIG_SOC_NRF5340_CPUAPP */

	/* Constant ePacket flags */
	epacket_global_flags_set(EPACKET_FLAGS_CLOUD_FORWARDING | EPACKET_FLAGS_CLOUD_SELF);
	epacket_udp_flags_set(EPACKET_FLAGS_UDP_ALWAYS_RX);

	/* Log Bluetooth connection events */
	auto_bluetooth_conn_log_configure(TDF_DATA_LOGGER_SERIAL, AUTO_BT_CONN_LOG_EVENTS_FLUSH);

	/* Log WiFi connection events */
	auto_wifi_conn_log_configure(TDF_DATA_LOGGER_SERIAL,
				     AUTO_WIFI_LOG_ALL | AUTO_WIFI_LOG_EVENTS_FLUSH);

	/* Start legacy Bluetooth advertising */
	bluetooth_legacy_advertising_run();

	/* Start watchdog */
	infuse_watchdog_start();

	/* Gateway receive handlers */
	epacket_set_receive_handler(epacket_serial, udp_backhaul_handler);
	epacket_set_receive_handler(epacket_bt_adv, udp_backhaul_handler);
	epacket_set_receive_handler(epacket_bt_central, udp_backhaul_handler);
	epacket_set_receive_handler(epacket_udp, udp_backhaul_handler);

	/* Always listening on Bluetooth advertising, serial and UDP */
	epacket_receive(epacket_serial, K_FOREVER);
	epacket_receive(epacket_bt_adv, K_FOREVER);
	epacket_receive(epacket_udp, K_FOREVER);

	/* Send key identifiers on boot */
	epacket_send_key_ids(epacket_serial, K_FOREVER);

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* Nothing further to do */
	k_sleep(K_FOREVER);
	return 0;
}
