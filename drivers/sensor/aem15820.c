/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT epeas_aem15820_sensor

#include <math.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/drivers/mfd/aem15820.h>
#include <infuse/drivers/sensor/aem15820.h>

#include <zephyr/logging/log.h>

#define AEM15820_T0_KELVIN 298.15f

/* APM data field layout (Table 60) */
#define APM_DATA_LEN      3
#define APM_COUNTER_MASK  GENMASK(22, 0)
#define APM_MANTISSA_MASK GENMASK(18, 0)
#define APM_OFFSET_SHIFT  19
#define APM_OFFSET_MASK   GENMASK(4, 0)

/* APMERR register */
#define APMERR_BSTLPOV   BIT(0)
#define APMERR_BSTLPNVLD BIT(1)
#define APMERR_BSTHPOV   BIT(2)
#define APMERR_LOADOV    BIT(4)
#define APMERR_LOADNVLD  BIT(5)

enum apm_monitor {
	APM_BOOST_LP,
	APM_BOOST_HP,
	APM_LOAD,
	APM_NUM,
};

struct apm_monitor_info {
	uint8_t overflow;
	uint8_t invalid;
	/* Monitor supports power meter mode (otherwise always counter mode) */
	bool power_mode;
};

static const struct apm_monitor_info apm_info[APM_NUM] = {
	[APM_BOOST_LP] = {APMERR_BSTLPOV, APMERR_BSTLPNVLD, true},
	[APM_BOOST_HP] = {APMERR_BSTHPOV, 0, false},
	[APM_LOAD] = {APMERR_LOADOV, APMERR_LOADNVLD, true},
};

struct aem15820_sensor_config {
	const struct device *mfd;
	float ntc_r0;
	float ntc_beta;
	float ntc_rdiv;
	/* Alpha in pW per LSB, 0 if not configured or the monitor is not enabled */
	uint32_t apm_alpha_pw[APM_NUM];
	/* Number of accumulated APM windows */
	uint16_t apm_windows;
	/* BSTLP and LOAD in power meter mode */
	bool apm_power_mode;
};

struct aem15820_sensor_data {
	/* Monitoring registers, contiguous from TEMP (0x28) to SRC (0x2A) */
	struct {
		uint8_t temp;
		uint8_t sto;
		uint8_t src;
	} __packed regs;
	/* APM registers, contiguous from APM0BSTLP (0x1C) to APMERR (0x27) */
	struct {
		uint8_t data[APM_NUM][APM_DATA_LEN];
		uint8_t chg5v[2];
		uint8_t err;
	} __packed apm;
};

LOG_MODULE_REGISTER(aem15820_sensor, CONFIG_SENSOR_LOG_LEVEL);

/* Datasheet Table 74. Codes outside the documented ranges are never reported. */
static int aem15820_src_microvolts(uint8_t code, int32_t *uv)
{
	int32_t c = code;

	if (c <= 0x06) {
		*uv = 113000;
	} else if (c <= 0x12) {
		*uv = 90000 + (2 * c - 9) * 7500;
	} else if (c <= 0x39) {
		*uv = 300000 + (2 * c - 37) * 15000;
	} else if ((c >= 0x68) && (c <= 0x79)) {
		*uv = (300000 + (2 * c - 165) * 15000) * 100 / 67;
	} else if ((c >= 0x9F) && (c <= 0xB9)) {
		*uv = (300000 + (2 * c - 293) * 15000) * 100 / 33;
	} else {
		return -EIO;
	}
	return 0;
}

static int aem15820_temp_celsius(const struct aem15820_sensor_config *config, uint8_t code,
				 float *celsius)
{
	float rth, inv_t;

	if (code == 0) {
		/* Thermistor shorted (or not yet measured) */
		return -EIO;
	}
	/* TH_MON = 256 * RTH / (RTH + RDIV) */
	rth = config->ntc_rdiv * code / (256 - code);
	/* Beta model: 1/T = 1/T0 + ln(RTH/R0)/B */
	inv_t = (1.0f / AEM15820_T0_KELVIN) + (logf(rth / config->ntc_r0) / config->ntc_beta);
	*celsius = (1.0f / inv_t) - 273.15f;
	return 0;
}

/* APM register value, scaled to a single window. Alpha converts the value to power. */
static int aem15820_apm_value(const struct aem15820_sensor_config *config,
			      const struct aem15820_sensor_data *data, enum apm_monitor mon,
			      float *value)
{
	const struct apm_monitor_info *info = &apm_info[mon];
	uint32_t raw = sys_get_le24(data->apm.data[mon]);
	uint64_t lsb;

	if (data->apm.err & info->invalid) {
		return -EIO;
	}
	if (data->apm.err & info->overflow) {
		return -ERANGE;
	}
	if ((info->power_mode && config->apm_power_mode) || (config->apm_windows > 1)) {
		/* Floating point format, mantissa << offset */
		lsb = (uint64_t)(raw & APM_MANTISSA_MASK)
		      << ((raw >> APM_OFFSET_SHIFT) & APM_OFFSET_MASK);
	} else {
		lsb = raw & APM_COUNTER_MASK;
	}
	*value = (float)lsb / config->apm_windows;
	return 0;
}

static int aem15820_sensor_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct aem15820_sensor_config *config = dev->config;
	struct aem15820_sensor_data *data = dev->data;
	int rc;

	/* Measurements are performed autonomously by the device, just read the latest values */
	rc = aem15820_reg_read_burst(config->mfd, AEM15820_REG_TEMP, &data->regs,
				     sizeof(data->regs));
	if (rc != 0) {
		return rc;
	}
	LOG_DBG("TEMP %02X STO %02X SRC %02X", data->regs.temp, data->regs.sto, data->regs.src);

	if (config->apm_alpha_pw[APM_BOOST_LP] || config->apm_alpha_pw[APM_BOOST_HP] ||
	    config->apm_alpha_pw[APM_LOAD]) {
		/* Data from the last completed APM window */
		rc = aem15820_reg_read_burst(config->mfd, AEM15820_REG_APM0BSTLP, &data->apm,
					     sizeof(data->apm));
		if (rc != 0) {
			return rc;
		}
		LOG_HEXDUMP_DBG(&data->apm, sizeof(data->apm), "APM");
	}

	if (IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG)) {
		uint8_t status[2];

		if (aem15820_reg_read_burst(config->mfd, AEM15820_REG_STATUS0, status,
					    sizeof(status)) == 0) {
			LOG_DBG("STATUS0 %02X STATUS1 %02X", status[0], status[1]);
		}
	}
	return 0;
}

static int aem15820_sensor_channel_get(const struct device *dev, enum sensor_channel chan,
				       struct sensor_value *val)
{
	const struct aem15820_sensor_config *config = dev->config;
	struct aem15820_sensor_data *data = dev->data;
	enum apm_monitor mon;
	int32_t uv;
	float temp;
	int rc;

	switch ((int)chan) {
	case SENSOR_CHAN_VOLTAGE:
	case SENSOR_CHAN_GAUGE_VOLTAGE:
		/* VSTO = 4.8 * DATA / 256 */
		return sensor_value_from_micro(val, 18750 * (int32_t)data->regs.sto);
	case SENSOR_CHAN_AEM15820_SRC_VOLTAGE:
		rc = aem15820_src_microvolts(data->regs.src, &uv);
		if (rc != 0) {
			return rc;
		}
		return sensor_value_from_micro(val, uv);
	case SENSOR_CHAN_GAUGE_TEMP:
		if (config->ntc_r0 == 0.0f) {
			return -ENOTSUP;
		}
		rc = aem15820_temp_celsius(config, data->regs.temp, &temp);
		if (rc != 0) {
			return rc;
		}
		return sensor_value_from_float(val, temp);
	case SENSOR_CHAN_AEM15820_BOOST_LP_POWER:
		mon = APM_BOOST_LP;
		break;
	case SENSOR_CHAN_AEM15820_BOOST_HP_POWER:
		mon = APM_BOOST_HP;
		break;
	case SENSOR_CHAN_AEM15820_LOAD_POWER:
		mon = APM_LOAD;
		break;
	default:
		return -ENOTSUP;
	}

	if (config->apm_alpha_pw[mon] == 0) {
		return -ENOTSUP;
	}
	rc = aem15820_apm_value(config, data, mon, &temp);
	if (rc != 0) {
		return rc;
	}
	/* pW to mW */
	return sensor_value_from_float(val, temp * config->apm_alpha_pw[mon] * 1e-9f);
}

static DEVICE_API(sensor, aem15820_sensor_api) = {
	.sample_fetch = aem15820_sensor_sample_fetch,
	.channel_get = aem15820_sensor_channel_get,
};

static int aem15820_sensor_init(const struct device *dev)
{
	const struct aem15820_sensor_config *config = dev->config;

	if (!device_is_ready(config->mfd)) {
		return -ENODEV;
	}
	return 0;
}

/* Thermistor and APM configuration live on the charger sibling node, summed so a missing node
 * gives 0
 */
#define AEM15820_CHARGER_PROP(node, prop)                                                          \
	+COND_CODE_1(DT_NODE_HAS_COMPAT(node, epeas_aem15820_charger),                             \
		     (DT_PROP_OR(node, prop, 0)), (0))
#define AEM15820_SIBLING_PROP(inst, prop)                                                          \
	(0 DT_FOREACH_CHILD_STATUS_OKAY_VARGS(DT_INST_PARENT(inst), AEM15820_CHARGER_PROP, prop))
#define AEM15820_CHARGER_ENUM(node, prop)                                                          \
	+COND_CODE_1(DT_NODE_HAS_COMPAT(node, epeas_aem15820_charger), (DT_ENUM_IDX(node, prop)),  \
		     (0))
#define AEM15820_APM_POWER_MODE(inst)                                                              \
	(0 DT_FOREACH_CHILD_STATUS_OKAY_VARGS(DT_INST_PARENT(inst), AEM15820_CHARGER_ENUM,         \
					      apm_mode))

/* Alpha is only used if the monitor is enabled on the charger */
#define AEM15820_APM_ALPHA(inst, alpha, enable)                                                    \
	(AEM15820_SIBLING_PROP(inst, enable) ? DT_INST_PROP_OR(inst, alpha, 0) : 0)

#define AEM15820_SENSOR_DEFINE(inst)                                                               \
	static struct aem15820_sensor_data aem15820_sensor_data_##inst;                            \
	static const struct aem15820_sensor_config aem15820_sensor_config_##inst = {               \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
		.ntc_r0 = AEM15820_SIBLING_PROP(inst, ntc_r0),                                     \
		.ntc_beta = AEM15820_SIBLING_PROP(inst, ntc_beta),                                 \
		.ntc_rdiv = AEM15820_SIBLING_PROP(inst, ntc_rdiv),                                 \
		.apm_alpha_pw =                                                                    \
			{                                                                          \
				[APM_BOOST_LP] = AEM15820_APM_ALPHA(                               \
					inst, apm_boost_lp_alpha_picowatt, apm_boost_lp),          \
				[APM_BOOST_HP] = AEM15820_APM_ALPHA(                               \
					inst, apm_boost_hp_alpha_picowatt, apm_boost_hp),          \
				[APM_LOAD] = AEM15820_APM_ALPHA(inst, apm_load_alpha_picowatt,     \
								apm_load),                         \
			},                                                                         \
		.apm_windows = MAX(AEM15820_SIBLING_PROP(inst, apm_accumulation), 1),              \
		.apm_power_mode = AEM15820_APM_POWER_MODE(inst),                                   \
	};                                                                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, aem15820_sensor_init, NULL,                             \
				     &aem15820_sensor_data_##inst, &aem15820_sensor_config_##inst, \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                     \
				     &aem15820_sensor_api);

DT_INST_FOREACH_STATUS_OKAY(AEM15820_SENSOR_DEFINE)
