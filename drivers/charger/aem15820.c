/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT epeas_aem15820_charger

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <infuse/drivers/mfd/aem15820.h>

#include <zephyr/logging/log.h>

/* Configuration registers written as a block, SRCREGU0 (0x01) to APMACC (0x14) */
#define CFG_FIRST    AEM15820_REG_SRCREGU0
#define CFG_LEN      (AEM15820_REG_APMACC - AEM15820_REG_SRCREGU0 + 1)
#define CFG_IDX(reg) ((reg) - CFG_FIRST)

/* Register fields */
#define SRCREGU0_MODE_MPPT   BIT(0)
#define SRCREGU0_CFG0(x)     ((x) << 1)
#define SRCREGU1_CFG1(x)     (x)
#define SRCREGU1_CFG2(x)     ((x) << 3)
#define BSTCFG_EN            BIT(0)
#define BSTCFG_RSVD          BIT(1)
#define BSTCFG_TMULT(x)      ((x) << 2)
#define BUCKCFG_VLOAD(x)     (x)
#define BUCKCFG_TMULT(x)     ((x) << 4)
#define CHG5V_EN             BIT(0)
#define CHG5V_CVEN           BIT(1)
#define CHG5V_THRESH(x)      ((x) << 2)
#define TEMPPROTECT_EN       BIT(0)
#define SRCLOW_THRESH_OFFSET 3
#define APM_BSTLPEN          BIT(0)
#define APM_BSTHPEN          BIT(1)
#define APM_LOADEN           BIT(2)
#define APM_CHG5VEN          BIT(3)
#define APM_MODE(x)          ((x) << 4)
#define APM_WINDOW(x)        ((x) << 5)
#define TEMPCOLD_RESET       0xD1
#define TEMPHOT_RESET        0x18
#define MPPT_RATIO_OFFSET    1

/* Storage element thresholds, V = base + THRESH * 18.75 mV (rounded to nearest) */
#define STO_STEP_UV                18750
#define STO_THRESH(mv, base_uv)    (((mv) * 1000 - (base_uv) + (STO_STEP_UV / 2)) / STO_STEP_UV)
#define STO_THRESH_UV(mv, base_uv) ((base_uv) + STO_THRESH(mv, base_uv) * STO_STEP_UV)
#define VOVDIS_BASE_UV             2400000
#define VOVDIS_MAX                 0x3F
#define VCHRDY_BASE_UV             2456000
#define VCHRDY_MAX                 0x63
#define VOVCH_BASE_UV              2700000
#define VOVCH_MAX                  0x65
#define VOVCH_THRESH(mv)           MIN(STO_THRESH(mv, VOVCH_BASE_UV), VOVCH_MAX)
#define VOVCH_THRESH_UV(mv)        (VOVCH_BASE_UV + VOVCH_THRESH(mv) * STO_STEP_UV)

/* Constant voltage regulation LVL codes (Table 26) */
#define SRC_CV_LVL_MIN 0x25
#define SRC_CV_LVL_MAX 0xC3

#define T0_KELVIN 298.15f

enum temp_threshold {
	TEMP_CHARGE_MIN,
	TEMP_CHARGE_MAX,
	TEMP_DISCHARGE_MIN,
	TEMP_DISCHARGE_MAX,
	TEMP_NUM,
};

static const uint8_t temp_regs[TEMP_NUM] = {
	[TEMP_CHARGE_MIN] = AEM15820_REG_TEMPCOLDCH,
	[TEMP_CHARGE_MAX] = AEM15820_REG_TEMPHOTCH,
	[TEMP_DISCHARGE_MIN] = AEM15820_REG_TEMPCOLDDIS,
	[TEMP_DISCHARGE_MAX] = AEM15820_REG_TEMPHOTDIS,
};

struct aem15820_charger_config {
	const struct device *mfd;
	/* Register values that are fully determined at compile time */
	uint8_t regs[CFG_LEN];
	float ntc_r0;
	float ntc_beta;
	float ntc_rdiv;
	int32_t temp_mc[TEMP_NUM];
	uint8_t temp_set;
	uint16_t src_cv_mv;
};

struct aem15820_charger_data {
	const struct device *dev;
	struct aem15820_irq_callback reset_cb;
	/* Serialises configuration updates */
	struct k_mutex lock;
	uint8_t regs[CFG_LEN];
	bool charge_enabled;
};

LOG_MODULE_REGISTER(aem15820_charger, CONFIG_CHARGER_LOG_LEVEL);

static int32_t src_cv_lvl_uv(int32_t lvl)
{
	if (lvl <= 0x73) {
		return 300000 + (lvl - 0x25) * 15000;
	} else if (lvl <= 0x94) {
		return (300000 + (lvl - 0x46) * 15000) * 100 / 67;
	}
	return (300000 + (lvl - 0x78) * 15000) * 100 / 33;
}

static uint8_t src_cv_lvl(uint16_t mv)
{
	int32_t target = 1000 * (int32_t)mv;
	int32_t best_err = INT32_MAX;
	uint8_t best = SRC_CV_LVL_MIN;

	for (int32_t lvl = SRC_CV_LVL_MIN; lvl <= SRC_CV_LVL_MAX; lvl++) {
		int32_t err = abs(src_cv_lvl_uv(lvl) - target);

		if (err < best_err) {
			best_err = err;
			best = lvl;
		}
	}
	return best;
}

static uint8_t temp_thresh(const struct aem15820_charger_config *config, int32_t millicelsius)
{
	float t = (millicelsius / 1000.0f) + 273.15f;
	float rth = config->ntc_r0 * expf(config->ntc_beta * ((1.0f / t) - (1.0f / T0_KELVIN)));
	float thresh = roundf(256.0f * rth / (rth + config->ntc_rdiv));

	return CLAMP(thresh, 0.0f, 255.0f);
}

static int aem15820_charger_apply(const struct device *dev)
{
	const struct aem15820_charger_config *config = dev->config;
	struct aem15820_charger_data *data = dev->data;
	uint8_t regs[CFG_LEN];
	int rc;

	memcpy(regs, data->regs, sizeof(regs));
	if (!data->charge_enabled) {
		/* High-power boost must be disabled whenever the low-power boost is */
		regs[CFG_IDX(AEM15820_REG_BSTHPCFG)] &= ~BSTCFG_EN;
		regs[CFG_IDX(AEM15820_REG_BSTLPCFG)] &= ~BSTCFG_EN;
		regs[CFG_IDX(AEM15820_REG_CHG5V)] &= ~CHG5V_EN;
	}

	rc = aem15820_reg_write_burst(config->mfd, CFG_FIRST, regs, sizeof(regs));
	if (rc != 0) {
		LOG_ERR("Configuration write failed (%d)", rc);
		return rc;
	}
	rc = aem15820_config_apply(config->mfd);
	if (rc != 0) {
		LOG_ERR("Configuration apply failed (%d)", rc);
		return rc;
	}
	LOG_HEXDUMP_DBG(regs, sizeof(regs), "Applied configuration");
	return 0;
}

static void aem15820_charger_reset(const struct device *mfd, struct aem15820_irq_callback *cb,
				   uint16_t events)
{
	struct aem15820_charger_data *data =
		CONTAINER_OF(cb, struct aem15820_charger_data, reset_cb);
	int rc;

	ARG_UNUSED(mfd);
	ARG_UNUSED(events);

	LOG_WRN("Device reset, re-applying configuration");
	k_mutex_lock(&data->lock, K_FOREVER);
	rc = aem15820_charger_apply(data->dev);
	k_mutex_unlock(&data->lock);
	if (rc != 0) {
		LOG_ERR("Re-apply failed (%d)", rc);
	}
}

static int aem15820_charger_get_property(const struct device *dev, const charger_prop_t prop,
					 union charger_propval *val)
{
	const struct aem15820_charger_config *config = dev->config;
	struct aem15820_charger_data *data = dev->data;
	uint8_t status[2];
	bool online;
	int rc;

	switch (prop) {
	case CHARGER_PROP_ONLINE:
	case CHARGER_PROP_STATUS:
	case CHARGER_PROP_HEALTH:
		break;
	default:
		return -ENOTSUP;
	}

	rc = aem15820_reg_read_burst(config->mfd, AEM15820_REG_STATUS0, status, sizeof(status));
	if (rc != 0) {
		return rc;
	}
	/* Harvesting source above VSRCLOW, or 5V input connected */
	online = !(status[0] & AEM15820_STATUS0_SRCLOW) || (status[0] & AEM15820_STATUS0_CHG5VCONN);

	switch (prop) {
	case CHARGER_PROP_ONLINE:
		val->online = online ? CHARGER_ONLINE_FIXED : CHARGER_ONLINE_OFFLINE;
		break;
	case CHARGER_PROP_STATUS:
		if (!data->charge_enabled) {
			val->status = CHARGER_STATUS_NOT_CHARGING;
		} else if (status[0] & AEM15820_STATUS0_OVCH) {
			val->status = CHARGER_STATUS_FULL;
		} else if (online) {
			val->status = CHARGER_STATUS_CHARGING;
		} else {
			val->status = CHARGER_STATUS_DISCHARGING;
		}
		break;
	default: /* CHARGER_PROP_HEALTH */
		if (!(data->regs[CFG_IDX(AEM15820_REG_TEMPPROTECT)] & TEMPPROTECT_EN)) {
			val->health = CHARGER_HEALTH_UNKNOWN;
		} else if (status[1] & AEM15820_STATUS1_TEMPCOLDCH) {
			val->health = CHARGER_HEALTH_COLD;
		} else if (status[1] & AEM15820_STATUS1_TEMPHOTCH) {
			val->health = CHARGER_HEALTH_HOT;
		} else {
			val->health = CHARGER_HEALTH_GOOD;
		}
		break;
	}
	return 0;
}

static int aem15820_charger_charge_enable(const struct device *dev, const bool enable)
{
	struct aem15820_charger_data *data = dev->data;
	bool previous;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	previous = data->charge_enabled;
	data->charge_enabled = enable;
	rc = aem15820_charger_apply(dev);
	if (rc != 0) {
		data->charge_enabled = previous;
	}
	k_mutex_unlock(&data->lock);
	return rc;
}

static DEVICE_API(charger, aem15820_charger_api) = {
	.get_property = aem15820_charger_get_property,
	.charge_enable = aem15820_charger_charge_enable,
};

static int aem15820_charger_init(const struct device *dev)
{
	const struct aem15820_charger_config *config = dev->config;
	struct aem15820_charger_data *data = dev->data;
	uint8_t lvl;
	int rc;

	if (!device_is_ready(config->mfd)) {
		return -ENODEV;
	}

	data->dev = dev;
	data->charge_enabled = true;
	k_mutex_init(&data->lock);
	memcpy(data->regs, config->regs, sizeof(data->regs));

	if (!(data->regs[CFG_IDX(AEM15820_REG_SRCREGU0)] & SRCREGU0_MODE_MPPT)) {
		/* LVL[2:0] in SRCREGU0.CFG0, LVL[5:3] in SRCREGU1.CFG1, LVL[7:6] in SRCREGU1.CFG2
		 */
		lvl = src_cv_lvl(config->src_cv_mv);
		LOG_DBG("SRC CV %u mV -> LVL %02X (%d uV)", config->src_cv_mv, lvl,
			src_cv_lvl_uv(lvl));
		data->regs[CFG_IDX(AEM15820_REG_SRCREGU0)] = SRCREGU0_CFG0(lvl & 0x07);
		data->regs[CFG_IDX(AEM15820_REG_SRCREGU1)] =
			SRCREGU1_CFG1((lvl >> 3) & 0x07) | SRCREGU1_CFG2(lvl >> 6);
	}
	for (int i = 0; i < TEMP_NUM; i++) {
		if (config->temp_set & BIT(i)) {
			data->regs[CFG_IDX(temp_regs[i])] = temp_thresh(config, config->temp_mc[i]);
		}
	}

	data->reset_cb.handler = aem15820_charger_reset;
	data->reset_cb.events = AEM15820_IRQ_I2CRDY;
	rc = aem15820_irq_callback_add(config->mfd, &data->reset_cb);
	if (rc == -ENOTSUP) {
		LOG_WRN("No IRQ line, configuration will be lost if the device resets");
	} else if (rc != 0) {
		return rc;
	}

	return aem15820_charger_apply(dev);
}

#define IS_MPPT(inst) DT_INST_ENUM_HAS_VALUE(inst, source_regulation, mppt)
#define HAS_NTC(inst) DT_INST_NODE_HAS_PROP(inst, ntc_r0)
#define VCHRDY_BUCK(inst)                                                                          \
	DT_INST_PROP_OR(inst, vchrdy_buck_millivolt, DT_INST_PROP(inst, vchrdy_millivolt))

/* Devicetree cells are unsigned, negative temperatures are two's complement */
#define TEMP_PROP(inst, prop)     ((int32_t)DT_INST_PROP_OR(inst, prop, 0))
#define TEMP_SET(inst, prop, idx) (DT_INST_NODE_HAS_PROP(inst, prop) ? BIT(idx) : 0)

#define AEM15820_CHARGER_ASSERTS(inst)                                                             \
	BUILD_ASSERT(!IS_MPPT(inst) || DT_INST_NODE_HAS_PROP(inst, mppt_ratio_percent),            \
		     "mppt-ratio-percent required for MPPT regulation");                           \
	BUILD_ASSERT(!IS_MPPT(inst) || DT_INST_NODE_HAS_PROP(inst, source_low_millivolt),          \
		     "source-low-millivolt required for MPPT regulation");                         \
	BUILD_ASSERT(IS_MPPT(inst) || DT_INST_NODE_HAS_PROP(inst, source_cv_millivolt),            \
		     "source-cv-millivolt required for constant voltage regulation");              \
	BUILD_ASSERT(IS_MPPT(inst) ||                                                              \
			     IN_RANGE(DT_INST_PROP_OR(inst, source_cv_millivolt, 0), 300, 4318),   \
		     "source-cv-millivolt out of range");                                          \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, vovdis_millivolt), 2400, 3581),                   \
		     "vovdis-millivolt out of range");                                             \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, vchrdy_millivolt), 2456, 4312),                   \
		     "vchrdy-millivolt out of range");                                             \
	BUILD_ASSERT(IN_RANGE(VCHRDY_BUCK(inst), 2456, 4312),                                      \
		     "vchrdy-buck-millivolt out of range");                                        \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, vovch_millivolt), 2700, 4594),                    \
		     "vovch-millivolt out of range");                                              \
	BUILD_ASSERT(STO_THRESH_UV(DT_INST_PROP(inst, vchrdy_millivolt), VCHRDY_BASE_UV) >=        \
			     STO_THRESH_UV(DT_INST_PROP(inst, vovdis_millivolt), VOVDIS_BASE_UV) + \
				     100000,                                                       \
		     "VCHRDY must be at least 100 mV above VOVDIS");                               \
	BUILD_ASSERT(VOVCH_THRESH_UV(DT_INST_PROP(inst, vovch_millivolt)) >=                       \
			     STO_THRESH_UV(DT_INST_PROP(inst, vchrdy_millivolt), VCHRDY_BASE_UV) + \
				     100000,                                                       \
		     "VOVCH must be at least 100 mV above VCHRDY");                                \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, chg5v_cv_millivolt, 0) * 1000 <                         \
			     VOVCH_THRESH_UV(DT_INST_PROP(inst, vovch_millivolt)),                 \
		     "chg5v-cv-millivolt must be below VOVCH");                                    \
	BUILD_ASSERT(!DT_INST_PROP(inst, boost_lp_disable) ||                                      \
			     DT_INST_PROP(inst, boost_hp_disable),                                 \
		     "High-power boost must be disabled with the low-power boost");                \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, apm_accumulation), 1, 256),                       \
		     "apm-accumulation out of range");                                             \
	BUILD_ASSERT(HAS_NTC(inst) ||                                                              \
			     !(DT_INST_NODE_HAS_PROP(inst, charge_temp_min_millicelsius) ||        \
			       DT_INST_NODE_HAS_PROP(inst, charge_temp_max_millicelsius) ||        \
			       DT_INST_NODE_HAS_PROP(inst, discharge_temp_min_millicelsius) ||     \
			       DT_INST_NODE_HAS_PROP(inst, discharge_temp_max_millicelsius)),      \
		     "Temperature thresholds require ntc-r0")

#define AEM15820_CHARGER_REGS(inst)                                                                \
	{                                                                                          \
		[CFG_IDX(AEM15820_REG_SRCREGU0)] =                                                 \
			IS_MPPT(inst) ? (SRCREGU0_MODE_MPPT |                                      \
					 SRCREGU0_CFG0(DT_INST_ENUM_IDX_OR(                        \
							       inst, mppt_ratio_percent, 0) +      \
						       MPPT_RATIO_OFFSET))                         \
				      : 0,                                                         \
		[CFG_IDX(AEM15820_REG_SRCREGU1)] =                                                 \
			IS_MPPT(inst) ? (SRCREGU1_CFG1(DT_INST_ENUM_IDX(inst, mppt_wait_us)) |     \
					 SRCREGU1_CFG2(DT_INST_ENUM_IDX(inst, mppt_period_ms)))    \
				      : 0,                                                         \
		[CFG_IDX(AEM15820_REG_VOVDIS)] =                                                   \
			STO_THRESH(DT_INST_PROP(inst, vovdis_millivolt), VOVDIS_BASE_UV),          \
		[CFG_IDX(AEM15820_REG_VCHRDY)] =                                                   \
			STO_THRESH(DT_INST_PROP(inst, vchrdy_millivolt), VCHRDY_BASE_UV),          \
		[CFG_IDX(AEM15820_REG_VOVCH)] = VOVCH_THRESH(DT_INST_PROP(inst, vovch_millivolt)), \
		[CFG_IDX(AEM15820_REG_BSTLPCFG)] =                                                 \
			BSTCFG_TMULT(DT_INST_ENUM_IDX(inst, boost_lp_timing_multiplier)) |         \
			BSTCFG_RSVD | (DT_INST_PROP(inst, boost_lp_disable) ? 0 : BSTCFG_EN),      \
		[CFG_IDX(AEM15820_REG_BSTHPCFG)] =                                                 \
			BSTCFG_TMULT(DT_INST_ENUM_IDX(inst, boost_hp_timing_multiplier)) |         \
			BSTCFG_RSVD | (DT_INST_PROP(inst, boost_hp_disable) ? 0 : BSTCFG_EN),      \
		[CFG_IDX(AEM15820_REG_BUCKCFG)] =                                                  \
			BUCKCFG_TMULT(DT_INST_ENUM_IDX(inst, buck_timing_multiplier)) |            \
			BUCKCFG_VLOAD(DT_INST_ENUM_IDX(inst, buck_output_millivolt)),              \
		[CFG_IDX(AEM15820_REG_VCHRDYBUCK)] =                                               \
			STO_THRESH(VCHRDY_BUCK(inst), VCHRDY_BASE_UV),                             \
		[CFG_IDX(AEM15820_REG_CHG5V)] =                                                    \
			CHG5V_THRESH(DT_INST_ENUM_IDX_OR(inst, chg5v_cv_millivolt, 0)) |           \
			(DT_INST_NODE_HAS_PROP(inst, chg5v_cv_millivolt) ? CHG5V_CVEN : 0) |       \
			(DT_INST_PROP(inst, chg5v_disable) ? 0 : CHG5V_EN),                        \
		[CFG_IDX(AEM15820_REG_TEMPCOLDCH)] = TEMPCOLD_RESET,                               \
		[CFG_IDX(AEM15820_REG_TEMPHOTCH)] = TEMPHOT_RESET,                                 \
		[CFG_IDX(AEM15820_REG_TEMPCOLDDIS)] = TEMPCOLD_RESET,                              \
		[CFG_IDX(AEM15820_REG_TEMPHOTDIS)] = TEMPHOT_RESET,                                \
		[CFG_IDX(AEM15820_REG_TEMPPROTECT)] =                                              \
			(HAS_NTC(inst) && !DT_INST_PROP(inst, thermal_protection_disable))         \
				? TEMPPROTECT_EN                                                   \
				: 0,                                                               \
		[CFG_IDX(AEM15820_REG_SRCLOW)] =                                                   \
			IS_MPPT(inst) ? (DT_INST_ENUM_IDX_OR(inst, source_low_millivolt, 0) +      \
					 SRCLOW_THRESH_OFFSET)                                     \
				      : 0,                                                         \
		[CFG_IDX(AEM15820_REG_APM)] =                                                      \
			(DT_INST_PROP(inst, apm_boost_lp) ? APM_BSTLPEN : 0) |                     \
			(DT_INST_PROP(inst, apm_boost_hp) ? APM_BSTHPEN : 0) |                     \
			(DT_INST_PROP(inst, apm_load) ? APM_LOADEN : 0) |                          \
			(DT_INST_PROP(inst, apm_chg5v) ? APM_CHG5VEN : 0) |                        \
			APM_MODE(DT_INST_ENUM_IDX(inst, apm_mode)) |                               \
			APM_WINDOW(DT_INST_ENUM_IDX(inst, apm_window_ms)),                         \
		[CFG_IDX(AEM15820_REG_APMACC)] = DT_INST_PROP(inst, apm_accumulation) - 1,         \
	}

#define AEM15820_CHARGER_DEFINE(inst)                                                              \
	AEM15820_CHARGER_ASSERTS(inst);                                                            \
	static struct aem15820_charger_data aem15820_charger_data_##inst;                          \
	static const struct aem15820_charger_config aem15820_charger_config_##inst = {             \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
		.regs = AEM15820_CHARGER_REGS(inst),                                               \
		.ntc_r0 = DT_INST_PROP_OR(inst, ntc_r0, 0),                                        \
		.ntc_beta = DT_INST_PROP(inst, ntc_beta),                                          \
		.ntc_rdiv = DT_INST_PROP(inst, ntc_rdiv),                                          \
		.temp_mc =                                                                         \
			{                                                                          \
				[TEMP_CHARGE_MIN] = TEMP_PROP(inst, charge_temp_min_millicelsius), \
				[TEMP_CHARGE_MAX] = TEMP_PROP(inst, charge_temp_max_millicelsius), \
				[TEMP_DISCHARGE_MIN] =                                             \
					TEMP_PROP(inst, discharge_temp_min_millicelsius),          \
				[TEMP_DISCHARGE_MAX] =                                             \
					TEMP_PROP(inst, discharge_temp_max_millicelsius),          \
			},                                                                         \
		.temp_set = TEMP_SET(inst, charge_temp_min_millicelsius, TEMP_CHARGE_MIN) |        \
			    TEMP_SET(inst, charge_temp_max_millicelsius, TEMP_CHARGE_MAX) |        \
			    TEMP_SET(inst, discharge_temp_min_millicelsius, TEMP_DISCHARGE_MIN) |  \
			    TEMP_SET(inst, discharge_temp_max_millicelsius, TEMP_DISCHARGE_MAX),   \
		.src_cv_mv = DT_INST_PROP_OR(inst, source_cv_millivolt, 0),                        \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, aem15820_charger_init, NULL, &aem15820_charger_data_##inst,    \
			      &aem15820_charger_config_##inst, POST_KERNEL,                        \
			      CONFIG_CHARGER_INIT_PRIORITY, &aem15820_charger_api);

DT_INST_FOREACH_STATUS_OKAY(AEM15820_CHARGER_DEFINE)
