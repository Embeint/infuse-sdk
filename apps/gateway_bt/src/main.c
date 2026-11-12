/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>

#include <infuse/auto/bluetooth_conn_log.h>
#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/work_q.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define EPACKET_BT_ADV        DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv))
#define EPACKET_BT_CENTRAL    DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central))
#define EPACKET_BT_PERIPHERAL DEVICE_DT_GET(DT_NODELABEL(epacket_bt_peripheral))

/* Maximum number of simultaneously connected backhaul clients */
#define MAX_BACKHAUL_CLIENTS 2

GATEWAY_HANDLER_DEFINE(bt_backhaul_handler, EPACKET_BT_PERIPHERAL);

#ifdef CONFIG_TASK_RUNNER_TASK_BATTERY
/* Report the battery state if the hardware supports measuring it */
#define BATTERY_TDFS        TASK_TDF_LOGGER_LOG_BATTERY
#define BATTERY_TASK_DEFINE (BATTERY_TASK, DEVICE_DT_GET(DT_ALIAS(fuel_gauge0)))
#else
#define BATTERY_TDFS 0
#define BATTERY_TASK_DEFINE
#endif /* CONFIG_TASK_RUNNER_TASK_BATTERY */

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = SEC_PER_MIN,
		.task_args.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_PERIPHERAL,
				.random_delay_ms = 1000,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | BATTERY_TDFS,
			},
	},
	{
		.task_id = TASK_ID_TDF_LOGGER_ALT1,
		.validity = TASK_VALID_PERMANENTLY_RUNS,
		.task_args.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV,
				.logging_period_ms = 2000,
				.random_delay_ms = 500,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | BATTERY_TDFS,
			},
	},
#ifdef CONFIG_TASK_RUNNER_TASK_BATTERY
	{
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 30,
	},
#endif /* CONFIG_TASK_RUNNER_TASK_BATTERY */
};

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL),
			 (TDF_LOGGER_ALT1_TASK, NULL), BATTERY_TASK_DEFINE);

#ifndef CONFIG_TASK_RUNNER_TASK_BATTERY
/* Battery channel is required by the task runner, normally defined by the battery task */
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
#endif /* CONFIG_TASK_RUNNER_TASK_BATTERY */

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0))
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif /* DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0)) */

static atomic_t backhaul_connected;

static void forwarding_disconnect(struct bt_conn *conn, void *user_data)
{
	struct bt_conn_info info;

	if ((bt_conn_get_info(conn, &info) != 0) || (info.role != BT_CONN_ROLE_CENTRAL)) {
		return;
	}
	if ((info.state == BT_CONN_STATE_CONNECTED) || (info.state == BT_CONN_STATE_CONNECTING)) {
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void backhaul_state_update(struct k_work *work)
{
	static bool scanning;
	bool connected = atomic_get(&backhaul_connected);
	int rc;

	if (!connected) {
		/* Connections to devices only exist to forward packets for backhaul clients.
		 * Without a backhaul, nothing will release them (connections created by
		 * `bt_connect_infuse` may have no timeout), so terminate them now.
		 */
		bt_conn_foreach(BT_CONN_TYPE_LE, forwarding_disconnect, NULL);
	}

	if (connected == scanning) {
		return;
	}

	/* Only scan for advertising packets while there is a backhaul to forward them on */
	rc = epacket_receive(EPACKET_BT_ADV, connected ? K_FOREVER : K_NO_WAIT);
	if (rc < 0) {
		LOG_WRN("Failed to %s scanning (%d)", connected ? "start" : "stop", rc);
		return;
	}
	LOG_INF("Scanning %s", connected ? "started" : "stopped");
	scanning = connected;
}

static K_WORK_DEFINE(backhaul_state_work, backhaul_state_update);

static void backhaul_state(uint16_t current_max_payload, void *user_ctx)
{
	atomic_set(&backhaul_connected, current_max_payload > 0);

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0))
	/* LED on while a backhaul connection exists */
	(void)gpio_pin_set_dt(&led0, current_max_payload > 0);
#endif /* DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0)) */

	/* Scanning and connection control can't run from the Bluetooth callback context */
	infuse_work_submit(&backhaul_state_work);
}

static void client_count(struct bt_conn *conn, void *user_data)
{
	struct bt_conn_info info;
	uint8_t *clients = user_data;

	if ((bt_conn_get_info(conn, &info) == 0) && (info.role == BT_CONN_ROLE_PERIPHERAL) &&
	    (info.state == BT_CONN_STATE_CONNECTED)) {
		*clients += 1;
	}
}

static void client_connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info;
	uint8_t clients = 0;

	if ((err != 0) || (bt_conn_get_info(conn, &info) != 0) ||
	    (info.role != BT_CONN_ROLE_PERIPHERAL)) {
		return;
	}

	/* Connectable advertising continues while clients are connected, so limit the number of
	 * clients to preserve connection resources for forwarding.
	 */
	bt_conn_foreach(BT_CONN_TYPE_LE, client_count, &clients);
	if (clients > MAX_BACKHAUL_CLIENTS) {
		LOG_WRN("Client limit reached, disconnecting new client");
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_LOW_RESOURCES);
	}
}

BT_CONN_CB_DEFINE(client_limit_cb) = {
	.connected = client_connected,
};

int main(void)
{
	static struct epacket_interface_cb backhaul_cb = {
		.interface_state = backhaul_state,
	};

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0))
	(void)gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
#endif /* DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0)) */

	/* Log Bluetooth connection events over the backhaul */
	auto_bluetooth_conn_log_configure(TDF_DATA_LOGGER_BT_PERIPHERAL,
					  AUTO_BT_CONN_LOG_EVENTS_FLUSH);

	/* Start watchdog */
	infuse_watchdog_start();

	/* Gateway receive handlers, Bluetooth peripheral connections are the backhaul */
	epacket_set_receive_handler(EPACKET_BT_PERIPHERAL, bt_backhaul_handler);
	epacket_set_receive_handler(EPACKET_BT_ADV, bt_backhaul_handler);
	epacket_set_receive_handler(EPACKET_BT_CENTRAL, bt_backhaul_handler);

	/* Track the backhaul connection state */
	epacket_register_callback(EPACKET_BT_PERIPHERAL, &backhaul_cb);

	/* Legacy advertising helps some devices (and iOS) connect */
	bluetooth_legacy_advertising_run();

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* No more work to do in this context */
	k_sleep(K_FOREVER);
	return 0;
}
