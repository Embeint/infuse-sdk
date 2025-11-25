/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/identifiers.h>
#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/zbus/channels.h>
#include <infuse/reboot.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

static void new_battery_data(const struct zbus_channel *chan);

TDF_LORA_RX_VAR(lora_rx_256, 256);
TDF_LORA_TX_VAR(lora_tx_64, 64);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY);
ZBUS_LISTENER_DEFINE(battery_listener, new_battery_data);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_BATTERY), battery_listener, 5);

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_NODELABEL(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_NODELABEL(led2), gpios);
static struct k_work_delayable led_disable;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
#if DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0))
	{
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 5,
	},
#endif /* DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0)) */
#ifdef CONFIG_TASK_RUNNER_TASK_GNSS
	{
		.task_id = TASK_ID_GNSS,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 5 * SEC_PER_MIN,
		.timeout_s = SEC_PER_MIN,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_FLASH,
					.tdf_mask = TASK_GNSS_LOG_PVT,
				},
			},
		.task_args.infuse.gnss =
			{
				.constellations =
					GNSS_SYSTEM_GPS | GNSS_SYSTEM_QZSS | GNSS_SYSTEM_SBAS,
				.flags = TASK_GNSS_FLAGS_PERFORMANCE_MODE |
					 TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX,
				.accuracy_m = 5,
				.position_dop = 40,
			},
	},
#endif /* CONFIG_TASK_RUNNER_TASK_GNSS */
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV,
				.logging_period_ms = 900,
				.random_delay_ms = 200,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_LOCATION,
			},
	},
};

#if DT_NODE_EXISTS(DT_ALIAS(gnss))
#define GNSS_TASK_DEFINE (GNSS_TASK, DEVICE_DT_GET(DT_ALIAS(gnss)))
#else
#define GNSS_TASK_DEFINE
#endif
#if DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0))
#define BAT_TASK_DEFINE (GNSS_TASK, DEVICE_DT_GET(DT_ALIAS(fuel_gauge0)))
#else
#define BAT_TASK_DEFINE
#endif

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL), GNSS_TASK_DEFINE,
			 BAT_TASK_DEFINE);

static void leds_disable(struct k_work *work)
{
	gpio_pin_set_dt(&led0, 0);
	gpio_pin_set_dt(&led1, 0);
	gpio_pin_set_dt(&led2, 0);
}

static void new_battery_data(const struct zbus_channel *chan)
{
	const struct tdf_battery_state *bat = zbus_chan_const_msg(chan);

	if (bat->current_ua > 5000) {
		/* Enable the charging LED */
		gpio_pin_set_dt(&led2, 1);
		k_work_reschedule(&led_disable, K_MSEC(500));
	}
}

static void lora_receive_cb(const struct device *dev, uint8_t *data, uint16_t size, int16_t rssi,
			    int8_t snr, void *user_data)
{
	struct lora_rx_256 rx_tdf;
	uint16_t log_len = MIN(size, sizeof(rx_tdf.payload));

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	/* Populate TDF */
	rx_tdf.snr = snr;
	rx_tdf.rssi = rssi;
	memcpy(rx_tdf.payload, data, log_len);

	/* Push the TDF */
	tdf_data_logger_log(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL |
				    TDF_DATA_LOGGER_FLASH,
			    TDF_LORA_RX, sizeof(struct tdf_lora_rx) + log_len, 0, &rx_tdf);
	tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL);

	/* Enable the RX LED */
	gpio_pin_set_dt(&led1, 1);
	k_work_reschedule(&led_disable, K_MSEC(500));

	LOG_INF("LoRa RX RSSI: %d dBm, SNR: %d dB", rssi, snr);
	LOG_HEXDUMP_INF(data, size, "LoRa RX payload");
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_ALIAS(lora0));
	struct lora_modem_config config = {
		.iq_inverted = false,
		.public_network = false,
	};
	static const struct kv_lora_config kv_config_default = {
		.frequency = 865100000,
		.bandwidth = BW_125_KHZ,
		.spreading_factor = SF_10,
		.coding_rate = CR_4_5,
		.preamble_len = 8,
		.tx_power = 30,
		/* Use default sync word */
		.sync_word = 0,
	};
	const struct lora_recv_async_callbacks cb = {
		.recv = lora_receive_cb,
		.user_data = NULL,
	};
	static struct kv_lora_config kv_config;
	struct lora_tx_64 tx_tdf;
	uint16_t cnt = 0;
	uint16_t tx_len;
	int rc;

	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
	k_work_init_delayable(&led_disable, leds_disable);

	if (!device_is_ready(dev)) {
		/* Device not ready, flash LED slowly then reboot */
		for (int i = 0; i < 30; i++) {
			gpio_pin_toggle_dt(&led0);
			k_sleep(K_SECONDS(1));
		}
		infuse_reboot(INFUSE_REBOOT_SW_WATCHDOG, (uintptr_t)dev, 0);
	}

	/* Start the watchdog */
	(void)infuse_watchdog_start();

	/* Start legacy advertising */
	bluetooth_legacy_advertising_run();

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* Light show on boot */
	for (int i = 0; i < 10; i++) {
		gpio_pin_toggle_dt(&led0);
		gpio_pin_toggle_dt(&led1);
		gpio_pin_toggle_dt(&led2);
		k_sleep(K_MSEC(250));
	}

	while (true) {
		/* Read configuration from the KV store */
		kv_store_read_fallback(KV_KEY_LORA_CONFIG, &kv_config, sizeof(kv_config),
				       &kv_config_default, sizeof(kv_config_default));
		config.frequency = kv_config.frequency;
		config.bandwidth = kv_config.bandwidth;
		config.datarate = kv_config.spreading_factor;
		config.coding_rate = kv_config.coding_rate;
		config.preamble_len = kv_config.preamble_len;
		config.tx_power = kv_config.tx_power;
		config.sync_word = kv_config.sync_word;

		/* Configure for RX */
		config.tx = false;
		rc = lora_config(dev, &config);
		if (rc < 0) {
			LOG_ERR("LoRa config failed");
		}

		/* Start receiving */
		rc = lora_recv_async(dev, &cb);
		if (rc < 0) {
			LOG_ERR("LoRa receive start failed");
		}

		/* Receive for 30 seconds */
		k_sleep(K_SECONDS(30));

		/* Stop receiving */
		rc = lora_recv_async(dev, NULL);
		if (rc < 0) {
			LOG_ERR("LoRa receive stop failed");
		}

		/* Configure for TX */
		config.tx = true;
		rc = lora_config(dev, &config);
		if (rc < 0) {
			LOG_ERR("LoRa config failed");
		}

		/* Populate payload */
		sys_put_le64(infuse_device_id(), tx_tdf.payload + 0);
		sys_put_le16(cnt, tx_tdf.payload + sizeof(uint64_t));
		tx_len = sizeof(uint64_t) + sizeof(uint16_t);
		cnt += 1;

		/* Send a payload */
		LOG_INF("Transmitting payload");
		rc = lora_send(dev, tx_tdf.payload, tx_len);
		if (rc < 0) {
			LOG_ERR("LoRa send failed");
		} else {
			/* Enable the TX LED */
			gpio_pin_set_dt(&led0, 1);
			k_work_reschedule(&led_disable, K_MSEC(500));

			/* Log the TDF */
			tdf_data_logger_log(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL |
						    TDF_DATA_LOGGER_FLASH,
					    TDF_LORA_TX, tx_len, 0, &tx_tdf);
			tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV |
					      TDF_DATA_LOGGER_BT_PERIPHERAL);
		}
	}
}
