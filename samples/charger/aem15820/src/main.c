/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/mfd/aem15820.h>
#include <infuse/drivers/sensor/aem15820.h>
#include <infuse/tdf/definitions.h>

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

#define TDF_LOGGERS (TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_SERIAL)

/* The accumulated TDF contains the preceding complete minute. */
#define ENERGY_ACCUMULATION_PERIOD_MS (60 * MSEC_PER_SEC)
/* 1 uWh = 3.6e9 nW.ms */
#define NW_MS_PER_UWH                 3600000000ULL

/* 1 mAh = 1e12 fAh */
#define FAH_PER_MAH 1000000000000ULL

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static K_SEM_DEFINE(apm_done, 0, 1);
/* Uptime of the last APM window completion */
static int64_t apm_done_ms;
/* Charge stored through the boost converters since boot */
static uint64_t stored_fah;
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

static void report_measurements(const struct device *sensor)
{
	struct tdf_energy_harvester_state harvester_tdf;
	struct tdf_storage_temperature temperature_tdf;
	int64_t sto_uv, src_uv, lp_nw, hp_nw, temperature_uc;
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
	if (rc != 0) {
		LOG_ERR("Harvester power unavailable (%d)", rc);
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

	TDF_DATA_LOGGER_LOG(TDF_LOGGERS, TDF_ENERGY_HARVESTER_STATE, 0, &harvester_tdf);
	TDF_DATA_LOGGER_LOG(TDF_LOGGERS, TDF_ENERGY_HARVESTER_ACCUMULATED, 0, &accumulated_tdf);
	TDF_DATA_LOGGER_LOG(TDF_LOGGERS, TDF_STORAGE_TEMPERATURE, 0, &temperature_tdf);
	tdf_data_logger_flush(TDF_LOGGERS);
}

static void energy_accumulate(uint64_t power_nw, uint32_t duration_ms)
{
	minute_energy_nw_ms += power_nw * duration_ms;
	minute_duration_ms += duration_ms;

	if (minute_duration_ms >= ENERGY_ACCUMULATION_PERIOD_MS) {
		/* duration has a schema scale of two seconds per LSB. */
		accumulated_tdf.duration = DIV_ROUND_CLOSEST(minute_duration_ms, 2 * MSEC_PER_SEC);
		accumulated_tdf.energy_uwh = DIV_ROUND_CLOSEST(minute_energy_nw_ms, NW_MS_PER_UWH);
		minute_energy_nw_ms = 0;
		minute_duration_ms = 0;
	}
}

static void apm_window_complete(const struct device *sensor, uint32_t window_ms)
{
	int64_t lp_nw, hp_nw, sto_uv;
	uint64_t total_pah;
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
		return;
	}
	energy_accumulate(lp_nw + hp_nw, window_ms);

	/* Charge into the storage element over the window, Q = P * t / VSTO.
	 * fAh = nW * ms * 1e-12 / (uV * 1e-6) / 3600 * 1e15 = nW * ms * 1e7 / (uV * 36)
	 */
	stored_fah += ((lp_nw + hp_nw) * window_ms * 10000000) / (sto_uv * 36);
	total_pah = stored_fah / 1000;

	LOG_INF("BOOST LP: %u.%03u uW, BOOST HP: %u.%03u uW, TOTAL: %u.%09u mAhr",
		(uint32_t)(lp_nw / 1000), (uint32_t)(lp_nw % 1000), (uint32_t)(hp_nw / 1000),
		(uint32_t)(hp_nw % 1000), (uint32_t)(stored_fah / FAH_PER_MAH),
		(uint32_t)(total_pah % 1000000000));
}

int main(void)
{
	const struct device *mfd = DEVICE_DT_GET(DT_PARENT(SENSOR_NODE));
	const struct device *sensor = DEVICE_DT_GET(SENSOR_NODE);
	const struct device *charger = DEVICE_DT_GET(CHARGER_NODE);
	k_timepoint_t next_report;
	int64_t last_apm_ms = -1;
	uint32_t window_ms;
	int rc;

	if (!device_is_ready(sensor) || !device_is_ready(charger)) {
		LOG_ERR("AEM15820 not ready");
		return -ENODEV;
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

	next_report = sys_timepoint_calc(K_NO_WAIT);
	for (;;) {
		rc = k_sem_take(&apm_done, sys_timepoint_timeout(next_report));

		/* Monitoring values are updated autonomously, fetch reads the latest */
		if (sensor_sample_fetch(sensor) != 0) {
			LOG_ERR("Sample fetch failed");
			continue;
		}
		if (rc == 0) {
			/* The actual APM window deviates from the nominal value, so integrate the
			 * average power over the measured time between completions (which covers
			 * all accumulated windows). The first period uses the nominal value.
			 */
			window_ms = (last_apm_ms < 0) ? APM_PERIOD_MS : (apm_done_ms - last_apm_ms);
			last_apm_ms = apm_done_ms;
			/* APM data is only valid until the next window completes */
			apm_window_complete(sensor, window_ms);
		}
		if (sys_timepoint_expired(next_report)) {
			report_measurements(sensor);
			next_report = sys_timepoint_calc(REPORT_PERIOD);
		}
	}
	return 0;
}
