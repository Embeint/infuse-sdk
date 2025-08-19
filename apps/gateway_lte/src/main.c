/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/auto/bluetooth_conn_log.h>
#include <infuse/auto/time_sync_log.h>
#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_udp.h>
#include <infuse/epacket/filter.h>
#include <infuse/epacket/packet.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/lib/memfault.h>
#include <infuse/states.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void custom_tdf_logger(uint8_t tdf_loggers, uint64_t timestamp);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 5 * SEC_PER_MIN,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_UDP,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_AMBIENT_ENV |
					TASK_TDF_LOGGER_LOG_LOCATION |
					TASK_TDF_LOGGER_LOG_NET_CONN | TASK_TDF_LOGGER_LOG_CUSTOM,
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
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_NET_CONN |
					TASK_TDF_LOGGER_LOG_LOCATION |
					TASK_TDF_LOGGER_LOG_AMBIENT_ENV,
			},
	},
	{
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 30,
	},
#if DT_NODE_EXISTS(DT_ALIAS(environmental0))
	{
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 30,
	},
#endif /* DT_NODE_EXISTS(DT_ALIAS(environmental0)) */
	{
		.task_id = TASK_ID_GNSS,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s =
			TASK_RUNNER_LOCKOUT_IGNORE_FIRST | (30 * SEC_PER_MIN),
		.timeout_s = SEC_PER_MIN,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_FLASH,
					.tdf_mask = TASK_GNSS_LOG_LLHA | TASK_GNSS_LOG_FIX_INFO,
				},
			},
		.task_args.infuse.gnss =
			{
				.flags = TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX |
					 TASK_GNSS_FLAGS_PERFORMANCE_MODE,
				/* FIX_OK: 1m accuracy, 10.0 PDOP */
				.accuracy_m = 1,
				.position_dop = 100,
			},
	},
};

#if DT_NODE_EXISTS(DT_ALIAS(environmental0))
#define ENV_TASK_DEFINE (ENVIRONMENTAL_TASK, DEVICE_DT_GET(DT_ALIAS(environmental0)))
#else
#define ENV_TASK_DEFINE
#endif

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, custom_tdf_logger),
			 (TDF_LOGGER_ALT1_TASK, NULL),
			 (BATTERY_TASK, DEVICE_DT_GET(DT_ALIAS(fuel_gauge0))), ENV_TASK_DEFINE,
			 (GNSS_TASK, DEVICE_DT_GET(DT_ALIAS(gnss))));

GATEWAY_HANDLER_DEFINE(udp_backhaul_handler, DEVICE_DT_GET(DT_NODELABEL(epacket_udp)));

#if DT_NODE_EXISTS(DT_ALIAS(led0))
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif

static void custom_tdf_logger(uint8_t tdf_loggers, uint64_t timestamp)
{
	ARG_UNUSED(timestamp);

	if (tdf_loggers & TDF_DATA_LOGGER_UDP) {
		/* Dump any pending Memfault chunks each time we send a UDP TDF */
		(void)infuse_memfault_queue_dump_all(K_MSEC(50));
	}
}

static void udp_interface_state(uint16_t current_max_payload, void *user_ctx)
{
	static bool first_conn = true;

	if (current_max_payload > 0 && first_conn) {
		LOG_INF("Reboot announce");
		/* When we first connect to the network, push an announce packet */
		task_tdf_logger_manual_run(
			TDF_DATA_LOGGER_UDP, 0,
			TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
				TASK_TDF_LOGGER_LOG_LOCATION | TASK_TDF_LOGGER_LOG_NET_CONN,
			NULL);
		tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
		first_conn = false;
	}
}

static void state_set(enum infuse_state state, bool already, uint16_t timeout, void *user_ctx)
{
	const struct device *bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	int rc;

	if ((state != INFUSE_STATE_HIGH_PRIORITY_UPLINK) || already) {
		return;
	}

	rc = epacket_receive(bt_adv, K_NO_WAIT);
	LOG_INF("Pausing scanning due to uplink (%d)", rc);
}

static void state_cleared(enum infuse_state state, void *user_ctx)
{
	const struct device *bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	int rc;

	if (state != INFUSE_STATE_HIGH_PRIORITY_UPLINK) {
		return;
	}

	rc = epacket_receive(bt_adv, K_FOREVER);
	LOG_INF("Resuming scanning (%d)", rc);
}

static void bluetooth_adv_handler(struct net_buf *buf)
{
	const struct device *udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));

	/* Forward 25% of Bluetooth advertising packets (0.25 * 255) */
	if (epacket_gateway_forward_filter(0, 64, buf)) {
		/* Forward packets that pass the filter */
		epacket_gateway_receive_handler(udp, buf);
	} else {
		/* Drop packets that don't */
		net_buf_unref(buf);
	}
}

int main(void)
{
	const struct device *bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	const struct device *bt_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	const struct device *udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));
	struct epacket_interface_cb udp_interface_cb = {
		.interface_state = udp_interface_state,
	};
	struct infuse_state_cb state_cb = {
		.state_set = state_set,
		.state_cleared = state_cleared,
	};

#if CONFIG_LTE_GATEWAY_DEFAULT_MAXIMUM_UPLINK_THROUGHPUT_KBPS > 0
	/* Set the default throughput to request from connected devices if it doesn't exist */
	if (!kv_store_key_exists(KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT)) {
		struct kv_bluetooth_throughput_limit limit = {
			CONFIG_LTE_GATEWAY_DEFAULT_MAXIMUM_UPLINK_THROUGHPUT_KBPS,
		};

		KV_STORE_WRITE(KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT, &limit);
	}
#endif /* CONFIG_LTE_GATEWAY_DEFAULT_MAXIMUM_UPLINK_THROUGHPUT_KBPS > 0 */

	/* State callbacks */
	infuse_state_register_callback(&state_cb);

	/* Constant ePacket flags */
	epacket_global_flags_set(EPACKET_FLAGS_CLOUD_FORWARDING | EPACKET_FLAGS_CLOUD_SELF);
	epacket_udp_flags_set(EPACKET_FLAGS_UDP_ALWAYS_RX);

	/* Start watchdog */
	infuse_watchdog_start();

	/* Log reboot events */
	tdf_reboot_info_log(TDF_DATA_LOGGER_FLASH | TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_UDP);

	/* Log LTE connection events */
	nrf_modem_monitor_network_state_log(TDF_DATA_LOGGER_FLASH);

	/* Configure time event logging */
	auto_time_sync_log_configure(TDF_DATA_LOGGER_FLASH,
				     AUTO_TIME_SYNC_LOG_SYNCS | AUTO_TIME_SYNC_LOG_REBOOT_ON_SYNC);
	auto_bluetooth_conn_log_configure(TDF_DATA_LOGGER_FLASH, 0);

	/* Start legacy Bluetooth advertising to workaround iOS and
	 * Nordic Softdevice connection issues.
	 */
	bluetooth_legacy_advertising_run();

	/* Setup reboot reporting */
	epacket_register_callback(udp, &udp_interface_cb);

	/* Gateway receive handlers */
	epacket_set_receive_handler(bt_adv, bluetooth_adv_handler);
	epacket_set_receive_handler(bt_central, udp_backhaul_handler);
	epacket_set_receive_handler(udp, udp_backhaul_handler);

	/* Always listening on Bluetooth advertising and UDP */
	epacket_receive(bt_adv, K_FOREVER);
	epacket_receive(udp, K_FOREVER);

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

#if DT_NODE_EXISTS(DT_ALIAS(led0))
	(void)gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);

	/* Boot LED sequence */
	for (int i = 0; i < 5; i++) {
		(void)gpio_pin_toggle_dt(&led0);
		k_sleep(K_MSEC(200));
	}
	gpio_pin_set_dt(&led0, 0);
#endif /* DT_NODE_EXISTS(DT_ALIAS(led0)) */

	/* Nothing further to do */
	k_sleep(K_FOREVER);
	return 0;
}
