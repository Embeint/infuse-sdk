/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/mfd/aem15820.h>
#include <infuse/drivers/sensor/aem15820.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/task_runner/tasks/tdf_logger.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>

#define CHARGER_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(epeas_aem15820_charger)
#define SENSOR_NODE  DT_COMPAT_GET_ANY_STATUS_OKAY(epeas_aem15820_sensor)

BUILD_ASSERT(DT_ENUM_HAS_VALUE(CHARGER_NODE, source_regulation, mppt),
	     "Sample requires MPPT source regulation");
BUILD_ASSERT(DT_SAME_NODE(DT_PARENT(CHARGER_NODE), DT_PARENT(SENSOR_NODE)),
	     "Sensor and charger must belong to the same AEM15820");

#define MPPT_RATIO_PERCENT DT_PROP(CHARGER_NODE, mppt_ratio_percent)
/* Nominal time between APM interrupts, including accumulated windows */
#define APM_PERIOD_MS                                                                              \
	(DT_PROP(CHARGER_NODE, apm_window_ms) * DT_PROP(CHARGER_NODE, apm_accumulation))
#define REPORT_PERIOD K_SECONDS(5)

/* Periodic reports are only sent to the live loggers, which are flushed on every report.
 * A report is additionally written to flash each time an accumulation period completes,
 * with flash blocks written as they fill.
 */
#define TDF_LOGGERS_LIVE (TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_SERIAL)
#define TDF_LOGGERS_ALL  (TDF_LOGGERS_LIVE | TDF_DATA_LOGGER_FLASH)

/* Optional ambient light sensor, reported alongside the AEM15820 measurements */
#define LIGHT_NODE DT_ALIAS(light0)

/* Optional LED, blinked on each report as a visual liveness indicator */
#define LED_BLINK_DURATION K_MSEC(20)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

/* The accumulated TDF contains the preceding complete minute. */
#define ENERGY_ACCUMULATION_PERIOD_MS (60 * MSEC_PER_SEC)
/* 1 uWh = 3.6e9 nW.ms */
#define NW_MS_PER_UWH                 3600000000ULL
/* 1 nWh = 3.6e6 nW.ms */
#define NW_MS_PER_NWH                 3600000ULL

/* 1 mAh = 1e12 fAh */
#define FAH_PER_MAH 1000000000000ULL

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static K_SEM_DEFINE(apm_done, 0, 1);
/* Uptime of the last APM window completion */
static int64_t apm_done_ms;
/* Charge stored through the boost converters since boot */
static uint64_t stored_fah;
/* Energy stored through each boost converter since boot */
static uint64_t stored_lp_nw_ms;
static uint64_t stored_hp_nw_ms;
/* Energy accumulated in the current, incomplete minute */
static uint64_t minute_energy_nw_ms;
static uint32_t minute_duration_ms;
/* Most recently completed minute, retransmitted with each report */
static struct tdf_energy_harvester_accumulated accumulated_tdf;

static void apm_handler(const struct device *dev, struct aem15820_irq_callback *cb, uint16_t events)
{
	apm_done_ms = k_uptime_get();
	k_sem_give(&apm_done);
}

static struct aem15820_irq_callback apm_cb = {
	.handler = apm_handler,
	.events = AEM15820_IRQ_APMDONE,
};

static int channel_get(const struct device *dev, int chan, int64_t *micro)
{
	struct sensor_value val;
	int rc;

	rc = sensor_channel_get(dev, (enum sensor_channel)chan, &val);
	if (rc == 0) {
		*micro = sensor_value_to_micro(&val);
	}
	return rc;
}

static void report_ambient_light(uint8_t loggers, uint64_t epoch_time)
{
#if DT_NODE_HAS_STATUS_OKAY(LIGHT_NODE)
	const struct device *light = DEVICE_DT_GET(LIGHT_NODE);
	struct tdf_ambient_light light_tdf;
	int64_t light_ulux;
	int rc;

	rc = sensor_sample_fetch_chan(light, SENSOR_CHAN_LIGHT);
	if (rc == 0) {
		rc = channel_get(light, SENSOR_CHAN_LIGHT, &light_ulux);
	}
	if (rc != 0) {
		LOG_ERR("Ambient light unavailable (%d)", rc);
		return;
	}

	light_tdf.illuminance = CLAMP(DIV_ROUND_CLOSEST(light_ulux, 1000), 0, UINT32_MAX);
	LOG_INF("Ambient light %6u lux", light_tdf.illuminance / 1000);
	TDF_DATA_LOGGER_LOG(loggers, TDF_AMBIENT_LIGHT, epoch_time, &light_tdf);
#endif /* DT_NODE_HAS_STATUS_OKAY(LIGHT_NODE) */
}

static void report_measurements(const struct device *sensor, uint8_t loggers)
{
	struct tdf_energy_harvester_state harvester_tdf;
	struct tdf_storage_temperature temperature_tdf;
	struct tdf_battery_state battery_tdf = {0};
	int64_t sto_uv, src_uv, lp_nw, hp_nw, load_nw, temperature_uc;
	uint64_t epoch_time;
	int rc;

	rc = channel_get(sensor, SENSOR_CHAN_VOLTAGE, &sto_uv);
	if (rc != 0) {
		LOG_ERR("STO voltage unavailable (%d)", rc);
		return;
	}
	rc = channel_get(sensor, SENSOR_CHAN_AEM15820_SRC_VOLTAGE, &src_uv);
	if (rc != 0) {
		LOG_ERR("SRC voltage unavailable (%d)", rc);
		return;
	}
	rc = channel_get(sensor, SENSOR_CHAN_AEM15820_BOOST_LP_POWER, &lp_nw);
	if (rc == 0) {
		rc = channel_get(sensor, SENSOR_CHAN_AEM15820_BOOST_HP_POWER, &hp_nw);
	}
	if (rc == 0) {
		rc = channel_get(sensor, SENSOR_CHAN_AEM15820_LOAD_POWER, &load_nw);
	}
	if (rc != 0) {
		LOG_ERR("APM power unavailable (%d)", rc);
		return;
	}
	rc = channel_get(sensor, SENSOR_CHAN_GAUGE_TEMP, &temperature_uc);
	if (rc != 0) {
		LOG_ERR("Storage temperature unavailable (%d)", rc);
		return;
	}

	/* SRC is the open-circuit voltage measured during the last MPPT evaluation,
	 * the source is then regulated to the configured ratio of that voltage.
	 */
	LOG_INF("STO %u mV, SRC %u mV, MPPT target %u mV (%d%%)", (uint32_t)(sto_uv / 1000),
		(uint32_t)(src_uv / 1000), (uint32_t)(src_uv * MPPT_RATIO_PERCENT / 100000),
		MPPT_RATIO_PERCENT);

	harvester_tdf.voltage_mv = DIV_ROUND_CLOSEST(src_uv, 1000);
	harvester_tdf.power_uw = DIV_ROUND_CLOSEST(lp_nw + hp_nw, 1000);
	/* Round microdegrees to centidegrees, including negative temperatures. */
	temperature_tdf.temperature =
		(temperature_uc + ((temperature_uc < 0) ? -5000 : 5000)) / 10000;
	/* Approximate the storage element current from the net power flow into STO,
	 * I = P / VSTO. uA = nW * 1e-9 / (uV * 1e-6) * 1e6 = nW * 1000 / uV.
	 * Buck conversion losses are ignored, and state of charge is not estimated.
	 */
	battery_tdf.voltage_mv = DIV_ROUND_CLOSEST(sto_uv, 1000);
	if (sto_uv > 0) {
		battery_tdf.current_ua = ((lp_nw + hp_nw - load_nw) * 1000) / sto_uv;
	}

	/* All measurements in the report share a single timestamp */
	epoch_time = epoch_time_now();
	/* Announce only needs to be sent live, first so it is in the first packet */
	task_tdf_logger_manual_run(TDF_LOGGERS_LIVE, epoch_time, TASK_TDF_LOGGER_LOG_ANNOUNCE,
				   NULL);
	TDF_DATA_LOGGER_LOG(loggers, TDF_BATTERY_STATE, epoch_time, &battery_tdf);
	TDF_DATA_LOGGER_LOG(loggers, TDF_ENERGY_HARVESTER_STATE, epoch_time, &harvester_tdf);
	TDF_DATA_LOGGER_LOG(loggers, TDF_ENERGY_HARVESTER_ACCUMULATED, epoch_time,
			    &accumulated_tdf);
	TDF_DATA_LOGGER_LOG(loggers, TDF_STORAGE_TEMPERATURE, epoch_time, &temperature_tdf);
	report_ambient_light(loggers, epoch_time);
	tdf_data_logger_flush(TDF_LOGGERS_LIVE);
}

/* Returns true when an accumulation period has completed */
static bool energy_accumulate(uint64_t power_nw, uint32_t duration_ms)
{
	minute_energy_nw_ms += power_nw * duration_ms;
	minute_duration_ms += duration_ms;

	if (minute_duration_ms >= ENERGY_ACCUMULATION_PERIOD_MS) {
		/* duration has a schema scale of two seconds per LSB. */
		accumulated_tdf.duration = DIV_ROUND_CLOSEST(minute_duration_ms, 2 * MSEC_PER_SEC);
		accumulated_tdf.energy_uwh = DIV_ROUND_CLOSEST(minute_energy_nw_ms, NW_MS_PER_UWH);
		minute_energy_nw_ms = 0;
		minute_duration_ms = 0;
		return true;
	}
	return false;
}

/* Returns true when an accumulation period has completed */
static bool apm_window_complete(const struct device *sensor, uint32_t window_ms)
{
	int64_t lp_nw, hp_nw, sto_uv;
	uint64_t total_nah, lp_nwh, hp_nwh;
	bool period_complete;
	int rc;

	/* Power channels are in mW, micro-mW is nW */
	rc = channel_get(sensor, SENSOR_CHAN_AEM15820_BOOST_LP_POWER, &lp_nw);
	if (rc == 0) {
		rc = channel_get(sensor, SENSOR_CHAN_AEM15820_BOOST_HP_POWER, &hp_nw);
	}
	if (rc == 0) {
		rc = channel_get(sensor, SENSOR_CHAN_VOLTAGE, &sto_uv);
	}
	if ((rc != 0) || (sto_uv <= 0)) {
		LOG_WRN("APM data unavailable (%d)", rc);
		return false;
	}
	period_complete = energy_accumulate(lp_nw + hp_nw, window_ms);

	/* Charge into the storage element over the window, Q = P * t / VSTO.
	 * fAh = nW * ms * 1e-12 / (uV * 1e-6) / 3600 * 1e15 = nW * ms * 1e7 / (uV * 36)
	 */
	stored_fah += ((lp_nw + hp_nw) * window_ms * 10000000) / (sto_uv * 36);
	total_nah = stored_fah / 1000000;
	stored_lp_nw_ms += lp_nw * window_ms;
	stored_hp_nw_ms += hp_nw * window_ms;
	lp_nwh = stored_lp_nw_ms / NW_MS_PER_NWH;
	hp_nwh = stored_hp_nw_ms / NW_MS_PER_NWH;

	LOG_INF("BOOST LP: %6u uW, BOOST HP: %6u uW, TOTAL: %u.%06u mAhr, LP: %u.%03u uWh, "
		"HP: %u.%03u uWh",
		(uint32_t)DIV_ROUND_CLOSEST(lp_nw, 1000), (uint32_t)DIV_ROUND_CLOSEST(hp_nw, 1000),
		(uint32_t)(stored_fah / FAH_PER_MAH), (uint32_t)(total_nah % 1000000),
		(uint32_t)(lp_nwh / 1000), (uint32_t)(lp_nwh % 1000), (uint32_t)(hp_nwh / 1000),
		(uint32_t)(hp_nwh % 1000));
	return period_complete;
}

static void led_blink(void)
{
	if (led.port == NULL) {
		return;
	}
	gpio_pin_set_dt(&led, 1);
	k_sleep(LED_BLINK_DURATION);
	gpio_pin_set_dt(&led, 0);
}

int main(void)
{
	const struct device *mfd = DEVICE_DT_GET(DT_PARENT(SENSOR_NODE));
	const struct device *sensor = DEVICE_DT_GET(SENSOR_NODE);
	const struct device *charger = DEVICE_DT_GET(CHARGER_NODE);
	k_timeout_t wdog_period;
	k_timepoint_t next_report;
	int wdog_channel;
	int64_t last_apm_ms = -1;
	uint32_t window_ms;
	bool persist;
	int rc;

	if (!device_is_ready(sensor) || !device_is_ready(charger)) {
		LOG_ERR("AEM15820 not ready");
		return -ENODEV;
	}
	if (led.port != NULL) {
		rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			LOG_WRN("LED unavailable (%d)", rc);
		}
	}

	/* Run the APM power integration from the APM window completion interrupt */
	rc = aem15820_irq_callback_add(mfd, &apm_cb);
	if (rc == 0) {
		rc = aem15820_irq_enable(mfd, AEM15820_IRQ_APMDONE, true);
	}
	if (rc != 0) {
		LOG_ERR("Failed to enable APM interrupt (%d)", rc);
		return rc;
	}

	/* Watchdog the main loop. The loop wakes at least every REPORT_PERIOD, which is
	 * much shorter than the watchdog period, so it is fed on every iteration.
	 */
	wdog_channel = infuse_watchdog_install(&wdog_period);
	if (wdog_channel >= 0) {
		infuse_watchdog_thread_register(wdog_channel, k_current_get());
	}
	(void)infuse_watchdog_start();

	next_report = sys_timepoint_calc(K_NO_WAIT);
	for (;;) {
		if (wdog_channel >= 0) {
			infuse_watchdog_feed(wdog_channel);
		}
		rc = k_sem_take(&apm_done, sys_timepoint_timeout(next_report));

		/* Monitoring values are updated autonomously, fetch reads the latest */
		if (sensor_sample_fetch(sensor) != 0) {
			LOG_ERR("Sample fetch failed");
			continue;
		}
		persist = false;
		if (rc == 0) {
			/* The actual APM window deviates from the nominal value, so integrate the
			 * average power over the measured time between completions (which covers
			 * all accumulated windows). The first period uses the nominal value.
			 */
			window_ms = (last_apm_ms < 0) ? APM_PERIOD_MS : (apm_done_ms - last_apm_ms);
			last_apm_ms = apm_done_ms;
			/* APM data is only valid until the next window completes */
			persist = apm_window_complete(sensor, window_ms);
		}
		/* Report immediately when an accumulation period completes, so that each
		 * completed period is written to flash alongside the current measurements.
		 */
		if (persist || sys_timepoint_expired(next_report)) {
			report_measurements(sensor, persist ? TDF_LOGGERS_ALL : TDF_LOGGERS_LIVE);
			led_blink();
			next_report = sys_timepoint_calc(REPORT_PERIOD);
		}
	}
	return 0;
}
