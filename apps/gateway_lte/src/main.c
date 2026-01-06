/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/pm/device_runtime.h>

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
#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(environmental0))
	{
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 30,
	},
#endif /* DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(environmental0)) */
#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(gnss))
	{
		.task_id = TASK_ID_GNSS,
		.validity = TASK_VALID_ALWAYS,
		.boot_lockout_minutes = 5,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s =
			TASK_RUNNER_LOCKOUT_IGNORE_FIRST | (30 * SEC_PER_MIN),
		.timeout_s = 2 * SEC_PER_MIN,
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
				.run_to_fix =
					{
						/* 1 minute to get some location knowledge */
						.any_fix_timeout = SEC_PER_MIN,
						/* Accuracy must improve by at least 1m every 10
						 * seconds after hitting 50m.
						 */
						.fix_plateau =
							{
								.min_accuracy_m = 50,
								.min_accuracy_improvement_m = 1,
								.timeout = 10,
							},
					},
				/* Gateways not expected to move */
				.dynamic_model = UBX_CFG_NAVSPG_DYNMODEL_STATIONARY,
			},
	},
#endif /* DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(gnss)) */
};

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(environmental0))
#define ENV_TASK_DEFINE                                                                            \
	(ENVIRONMENTAL_TASK, DEVICE_DT_GET(DT_ALIAS(environmental0)),                              \
	 DEVICE_DT_GET_OR_NULL(DT_ALIAS(environmental1)))
#else
#define ENV_TASK_DEFINE
#endif
#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(gnss))
#define GNSS_TASK_DEFINE (GNSS_TASK, DEVICE_DT_GET(DT_ALIAS(gnss)))
#else
#define GNSS_TASK_DEFINE
#endif

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, custom_tdf_logger),
			 (TDF_LOGGER_ALT1_TASK, NULL),
			 (BATTERY_TASK, DEVICE_DT_GET(DT_ALIAS(fuel_gauge0))), ENV_TASK_DEFINE,
			 GNSS_TASK_DEFINE);

GATEWAY_HANDLER_DEFINE(udp_backhaul_handler, DEVICE_DT_GET(DT_NODELABEL(epacket_udp)));

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0))
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif

/* Forward 25% of all Bluetooth packets by default, RSSI if whole packet dropped */
static const struct kv_gateway_bluetooth_forward_options bt_forwarding_options_default = {
	.flags = FILTER_FORWARD_RSSI_FALLBACK,
	.percent = 256 / 4,
};
static struct kv_gateway_bluetooth_forward_options bt_forwarding_options;

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

static void kv_value_changed(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	const struct kv_gateway_bluetooth_forward_options *options = data;

	if (key != KV_KEY_GATEWAY_BLUETOOTH_FORWARD_OPTIONS) {
		return;
	}

	if ((data == NULL) || (data_len != sizeof(bt_forwarding_options))) {
		/* Revert to the defaults */
		bt_forwarding_options = bt_forwarding_options_default;
	} else {
		/* Use configured values */
		bt_forwarding_options.flags = options->flags;
		bt_forwarding_options.percent = options->percent;
	}
}

static void bluetooth_adv_handler(struct net_buf *buf)
{
	const struct device *udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));

	/* Forward 25% of Bluetooth advertising packets (0.25 * 255) */
	if (epacket_gateway_forward_filter(bt_forwarding_options.flags,
					   bt_forwarding_options.percent, buf)) {
		/* Forward packets that pass the filter */
		epacket_gateway_receive_handler(udp, buf);
	} else {
		if (bt_forwarding_options.flags & FILTER_FORWARD_RSSI_FALLBACK) {
			struct epacket_rx_metadata *meta = net_buf_user_data(buf);
			struct tdf_infuse_bluetooth_rssi tdf_rssi = {
				.infuse_id = meta->packet_device_id,
				.rssi = meta->rssi,
			};

			/* Log the RSSI */
			TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_UDP, TDF_INFUSE_BLUETOOTH_RSSI,
					    epoch_time_now(), &tdf_rssi);
		}

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
	struct kv_store_cb kv_cb = {
		.value_changed = kv_value_changed,
	};
	int rc;

#if CONFIG_LTE_GATEWAY_DEFAULT_MAXIMUM_UPLINK_THROUGHPUT_KBPS > 0
	/* Set the default throughput to request from connected devices if it doesn't exist */
	if (!kv_store_key_exists(KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT)) {
		struct kv_bluetooth_throughput_limit limit = {
			CONFIG_LTE_GATEWAY_DEFAULT_MAXIMUM_UPLINK_THROUGHPUT_KBPS,
		};

		KV_STORE_WRITE(KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT, &limit);
	}
#endif /* CONFIG_LTE_GATEWAY_DEFAULT_MAXIMUM_UPLINK_THROUGHPUT_KBPS > 0 */

	/* Setup Bluetooth forwarding configuration */
	rc = kv_store_read_fallback(KV_KEY_GATEWAY_BLUETOOTH_FORWARD_OPTIONS,
				    &bt_forwarding_options, sizeof(bt_forwarding_options),
				    &bt_forwarding_options_default,
				    sizeof(bt_forwarding_options_default));
	if (rc < 0) {
		LOG_WRN("Setting Bluetooth forwarding options failed (%d)", rc);
		memcpy(&bt_forwarding_options, &bt_forwarding_options_default,
		       sizeof(bt_forwarding_options));
	}

	/* KV store callbacks */
	kv_store_register_callback(&kv_cb);

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
	lte_modem_monitor_network_state_log(TDF_DATA_LOGGER_FLASH);

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

#ifdef CONFIG_MODEM_CELLULAR
	/* For now the Cellular Modem abstraction is not linked to a connection manager */
	pm_device_runtime_get(DEVICE_DT_GET(DT_ALIAS(modem)));
#endif /* CONFIG_MODEM_CELLULAR */

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0))
	(void)gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);

	/* Boot LED sequence */
	for (int i = 0; i < 5; i++) {
		(void)gpio_pin_toggle_dt(&led0);
		k_sleep(K_MSEC(200));
	}
	gpio_pin_set_dt(&led0, 0);
#endif /* DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0)) */

	/* Nothing further to do */
	k_sleep(K_FOREVER);
	return 0;
}
