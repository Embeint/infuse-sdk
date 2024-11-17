/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT zephyr_fuel_gauge_composite

#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/battery.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/kernel.h>

struct composite_config {
	const struct device *battery_voltage;
	const struct device *battery_charge_current;
	int32_t ocv_lookup_table[BATTERY_OCV_TABLE_LEN];
	uint32_t charge_capacity_microamp_hours;
	enum battery_chemistry chemistry;
};

struct composite_data {
	int voltage_val;
	k_ticks_t voltage_time;
};

static int composite_read_micro(const struct device *dev, enum sensor_channel chan, int *val)
{
	struct sensor_value sensor_val;
	int rc;

	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		return rc;
	}
	rc = sensor_sample_fetch(dev);
	if (rc < 0) {
		return rc;
	}
	rc = sensor_channel_get(dev, chan, &sensor_val);
	if (rc < 0) {
		return rc;
	}
	rc = pm_device_runtime_put(dev);
	if (rc < 0) {
		return rc;
	}
	*val = sensor_value_to_micro(&sensor_val);
	return 0;
}

static int composite_get_prop(const struct device *dev, fuel_gauge_prop_t prop,
			      union fuel_gauge_prop_val *val)
{
	const struct composite_config *config = dev->config;
	struct composite_data *data = dev->data;
	int voltage, rc = -ENOTSUP;

	switch (prop) {
	case FUEL_GAUGE_FULL_CHARGE_CAPACITY:
		if (config->charge_capacity_microamp_hours > 0) {
			val->full_charge_capacity = config->charge_capacity_microamp_hours;
			rc = 0;
		}
		break;
	case FUEL_GAUGE_DESIGN_CAPACITY:
		if (config->charge_capacity_microamp_hours > 0) {
			val->full_charge_capacity = config->charge_capacity_microamp_hours / 1000;
			rc = 0;
		}
		break;
	case FUEL_GAUGE_VOLTAGE:
		rc = composite_read_micro(config->battery_voltage, SENSOR_CHAN_VOLTAGE,
					  &val->voltage);
		if (rc == 0) {
			data->voltage_val = val->voltage;
			data->voltage_time = k_uptime_ticks();
		}
		break;
	case FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE:
	case FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE:
		if (config->ocv_lookup_table[0] == -1) {
			break;
		}
		if (data->voltage_time &&
		    (k_ticks_to_ms_near32(k_uptime_ticks() - data->voltage_time) < 100)) {
			/* Re-use the latest voltage measurement rather than sampling again */
			voltage = data->voltage_val;
			rc = 0;
		} else {
			/* Measure voltage in this call */
			rc = composite_read_micro(config->battery_voltage, SENSOR_CHAN_VOLTAGE,
						  &voltage);
		}
		/* Convert voltage to state of charge */
		val->relative_state_of_charge =
			battery_soc_lookup(config->ocv_lookup_table, voltage) / 1000;
		break;
	case FUEL_GAUGE_CURRENT:
		if (config->battery_charge_current) {
			rc = composite_read_micro(config->battery_charge_current,
						  SENSOR_CHAN_CURRENT, &val->current);
		}
		break;
	}

	return rc;
}

static int composite_init(const struct device *dev)
{
	return 0;
}

static const struct fuel_gauge_driver_api composite_api = {
	.get_property = composite_get_prop,
	.set_property = NULL,
	.get_buffer_property = NULL,
	.battery_cutoff = NULL,
};

#define COMPOSITE_INIT(inst)                                                                       \
	static const struct composite_config composite_##inst##_config = {                         \
		.battery_voltage = DEVICE_DT_GET(DT_INST_PROP(inst, battery_voltage)),             \
		.battery_charge_current =                                                          \
			DEVICE_DT_GET_OR_NULL(DT_INST_PROP(inst, battery_charge_current)),         \
		.ocv_lookup_table =                                                                \
			BATTERY_OCV_TABLE_DT_GET(DT_DRV_INST(inst), ocv_capacity_table_0),         \
		.charge_capacity_microamp_hours =                                                  \
			DT_INST_PROP_OR(inst, charge_full_design_microamp_hours, 0),               \
		.chemistry = BATTERY_CHEMISTRY_DT_GET(inst),                                       \
	};                                                                                         \
	static struct composite_data composite_##inst##_data;                                      \
	DEVICE_DT_INST_DEFINE(inst, composite_init, NULL, &composite_##inst##_data,                \
			      &composite_##inst##_config, POST_KERNEL,                             \
			      CONFIG_SENSOR_INIT_PRIORITY, &composite_api);

DT_INST_FOREACH_STATUS_OKAY(COMPOSITE_INIT)
