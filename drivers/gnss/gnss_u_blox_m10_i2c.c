/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * Uses the NAV-PVT message to fulfill the requirements of the GNSS API.
 */

#define DT_DRV_COMPAT u_blox_m10_i2c

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/modem/backend/u_blox_i2c.h>
#include <infuse/gnss/ubx/defines.h>
#include <infuse/gnss/ubx/cfg.h>
#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/protocol.h>

#include "u_blox_common.h"

#define SYNC_MESSAGE_TIMEOUT K_MSEC(250)

struct ubx_m10_i2c_config {
	struct ubx_common_config common;
	struct i2c_dt_spec i2c;
};

struct ubx_m10_i2c_data {
	struct ubx_common_data common;
	/* I2C modem backend */
	struct modem_backend_ublox_i2c i2c_backend;
};

BUILD_ASSERT(__alignof(struct ubx_frame) == 1);

LOG_MODULE_DECLARE(ubx_modem);

#ifndef CONFIG_GNSS_U_BLOX_NO_API_COMPAT

static int get_fix_rate_handler(uint8_t message_class, uint8_t message_id, const void *payload,
				size_t payload_len, void *user_data)
{
	const struct ubx_msg_cfg_valget_response *valget = payload;
	uint32_t *fix_interval_ms = user_data;
	const uint8_t *val_ptr = valget->cfg_data;
	size_t val_len = payload_len - sizeof(*valget);
	struct ubx_cfg_val cfg_val;
	uint16_t meas = 0;
	uint16_t nav = 0;

	__ASSERT_NO_MSG(valget->version == 0x01);

	while (ubx_cfg_val_parse(&val_ptr, &val_len, &cfg_val) == 0) {
		if (cfg_val.key == UBX_CFG_KEY_RATE_MEAS) {
			meas = cfg_val.val.u2;
		} else if (cfg_val.key == UBX_CFG_KEY_RATE_NAV) {
			nav = cfg_val.val.u2;
		}
	}
	/* Output interval is measurement period * solution ratio */
	*fix_interval_ms = (meas * nav);
	/* Valid if both params were returned */
	return (meas == 0) || (nav == 0) ? -EINVAL : 0;
}

static int ubx_m10_i2c_get_fix_rate(const struct device *dev, uint32_t *fix_interval_ms)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	struct ubx_m10_i2c_data *data = dev->data;

	ubx_msg_prepare_valget(&cfg_buf, UBX_MSG_CFG_VALGET_LAYER_RAM, 0);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_RATE_MEAS);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_RATE_NAV);
	ubx_msg_finalise(&cfg_buf);

	return ubx_modem_send_sync(&data->common.modem, &cfg_buf, UBX_HANDLING_RSP_ACK,
				   get_fix_rate_handler, fix_interval_ms, SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m10_i2c_set_fix_rate(const struct device *dev, uint32_t fix_interval_ms)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 32);
	struct ubx_m10_i2c_data *data = dev->data;

	if ((fix_interval_ms < 25) || (fix_interval_ms > UINT16_MAX)) {
		return -EINVAL;
	}

	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_RATE_MEAS, fix_interval_ms);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_RATE_NAV, 1);
	ubx_msg_finalise(&cfg_buf);
	return ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
}

static int get_navigation_mode_handler(uint8_t message_class, uint8_t message_id,
				       const void *payload, size_t payload_len, void *user_data)
{
	const struct ubx_msg_cfg_valget_response *valget = payload;
	enum gnss_navigation_mode *mode = user_data;
	const uint8_t *val_ptr = valget->cfg_data;
	size_t val_len = payload_len - sizeof(*valget);
	struct ubx_cfg_val cfg_val;

	__ASSERT_NO_MSG(valget->version == 0x01);

	while (ubx_cfg_val_parse(&val_ptr, &val_len, &cfg_val) == 0) {
		if (cfg_val.key == UBX_CFG_KEY_NAVSPG_DYNMODEL) {
			switch (cfg_val.val.e1) {
			case UBX_CFG_NAVSPG_DYNMODEL_STATIONARY:
				*mode = GNSS_NAVIGATION_MODE_ZERO_DYNAMICS;
				break;
			case UBX_CFG_NAVSPG_DYNMODEL_PEDESTRIAN:
			case UBX_CFG_NAVSPG_DYNMODEL_AUTOMOTIVE:
			case UBX_CFG_NAVSPG_DYNMODEL_MOWER:
				*mode = GNSS_NAVIGATION_MODE_LOW_DYNAMICS;
				break;
			case UBX_CFG_NAVSPG_DYNMODEL_AIRBORNE4G:
			case UBX_CFG_NAVSPG_DYNMODEL_BIKE:
			case UBX_CFG_NAVSPG_DYNMODEL_ESCOOTER:
				*mode = GNSS_NAVIGATION_MODE_HIGH_DYNAMICS;
				break;
			default:
				*mode = GNSS_NAVIGATION_MODE_BALANCED_DYNAMICS;
				break;
			};
			return 0;
		}
	}
	/* Key didn't exist */
	return -EINVAL;
}

static int ubx_m10_i2c_get_navigation_mode(const struct device *dev,
					   enum gnss_navigation_mode *mode)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	struct ubx_m10_i2c_data *data = dev->data;

	ubx_msg_prepare_valget(&cfg_buf, UBX_MSG_CFG_VALGET_LAYER_RAM, 0);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_NAVSPG_DYNMODEL);
	ubx_msg_finalise(&cfg_buf);

	return ubx_modem_send_sync(&data->common.modem, &cfg_buf, UBX_HANDLING_RSP_ACK,
				   get_navigation_mode_handler, mode, SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m10_i2c_set_navigation_mode(const struct device *dev, enum gnss_navigation_mode mode)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 32);
	struct ubx_m10_i2c_data *data = dev->data;
	uint8_t ubx_dynmodel;

	switch (mode) {
	case GNSS_NAVIGATION_MODE_ZERO_DYNAMICS:
		ubx_dynmodel = UBX_CFG_NAVSPG_DYNMODEL_STATIONARY;
		break;
	case GNSS_NAVIGATION_MODE_LOW_DYNAMICS:
		ubx_dynmodel = UBX_CFG_NAVSPG_DYNMODEL_PORTABLE;
		break;
	case GNSS_NAVIGATION_MODE_HIGH_DYNAMICS:
		ubx_dynmodel = UBX_CFG_NAVSPG_DYNMODEL_AIRBORNE4G;
		break;
	default:
		ubx_dynmodel = UBX_CFG_NAVSPG_DYNMODEL_AIRBORNE1G;
		break;
	}

	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_NAVSPG_DYNMODEL, ubx_dynmodel);
	ubx_msg_finalise(&cfg_buf);
	return ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
}

#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */

#ifdef GNSS_UBX_CONSTELLATION_CONFIG

static int get_enabled_systems_handler(uint8_t message_class, uint8_t message_id,
				       const void *payload, size_t payload_len, void *user_data)
{
	const struct ubx_msg_cfg_valget_response *valget = payload;
	gnss_systems_t *systems = user_data;
	const uint8_t *val_ptr = valget->cfg_data;
	size_t val_len = payload_len - sizeof(*valget);
	struct ubx_cfg_val cfg_val;
	gnss_systems_t out = 0;

	__ASSERT_NO_MSG(valget->version == 0x01);

	while (ubx_cfg_val_parse(&val_ptr, &val_len, &cfg_val) == 0) {
		/* Nothing to do if key isn't enabled */
		if (!cfg_val.val.l) {
			continue;
		}
		switch (cfg_val.key) {
		case UBX_CFG_KEY_SIGNAL_GPS_ENA:
			out |= GNSS_SYSTEM_GPS;
			break;
		case UBX_CFG_KEY_SIGNAL_GALILEO_ENA:
			out |= GNSS_SYSTEM_GALILEO;
			break;
		case UBX_CFG_KEY_SIGNAL_BEIDOU_ENA:
			out |= GNSS_SYSTEM_BEIDOU;
			break;
		case UBX_CFG_KEY_SIGNAL_GLONASS_ENA:
			out |= GNSS_SYSTEM_GLONASS;
			break;
		case UBX_CFG_KEY_SIGNAL_SBAS_ENA:
			out |= GNSS_SYSTEM_SBAS;
			break;
		case UBX_CFG_KEY_SIGNAL_QZSS_ENA:
			out |= GNSS_SYSTEM_QZSS;
			break;
		default:
			break;
		}
	}
	*systems = out;
	return 0;
}

static int ubx_m10_i2c_get_enabled_systems(const struct device *dev, gnss_systems_t *systems)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	struct ubx_m10_i2c_data *data = dev->data;

	ubx_msg_prepare_valget(&cfg_buf, UBX_MSG_CFG_VALGET_LAYER_RAM, 0);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_SIGNAL_GPS_ENA);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_SIGNAL_BEIDOU_ENA);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_SIGNAL_GALILEO_ENA);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_SIGNAL_GLONASS_ENA);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_SIGNAL_SBAS_ENA);
	net_buf_simple_add_le32(&cfg_buf, UBX_CFG_KEY_SIGNAL_QZSS_ENA);
	ubx_msg_finalise(&cfg_buf);

	return ubx_modem_send_sync(&data->common.modem, &cfg_buf, UBX_HANDLING_RSP_ACK,
				   get_enabled_systems_handler, systems, SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m10_i2c_set_enabled_systems(const struct device *dev, gnss_systems_t s)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	struct ubx_m10_i2c_data *data = dev->data;
	const gnss_systems_t major =
		GNSS_SYSTEM_GPS | GNSS_SYSTEM_BEIDOU | GNSS_SYSTEM_GALILEO | GNSS_SYSTEM_GLONASS;
	int rc;

	/* At least one major constellation must be enabled */
	if (!(s & major)) {
		return -EINVAL;
	}
	/* Integration manual recommends enabling QZSS with GPS */
	if ((s & GNSS_SYSTEM_GPS) & !(s & GNSS_SYSTEM_QZSS)) {
		LOG_WRN("It is recommended to enable QZSS together with GPS");
	}

	/* Leave individual signal configuration at their default values */
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_SIGNAL_GPS_ENA, s & GNSS_SYSTEM_GPS);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_SIGNAL_BEIDOU_ENA, s & GNSS_SYSTEM_BEIDOU);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_SIGNAL_GALILEO_ENA, s & GNSS_SYSTEM_GALILEO);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_SIGNAL_GLONASS_ENA, s & GNSS_SYSTEM_GLONASS);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_SIGNAL_SBAS_ENA, s & GNSS_SYSTEM_SBAS);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_SIGNAL_QZSS_ENA, s & GNSS_SYSTEM_QZSS);
	ubx_msg_finalise(&cfg_buf);
	rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc == 0) {
		/* Integration guide specifies a 0.5 second delay after changing GNSS config
		 */
		k_sleep(K_MSEC(500));
	}
	return rc;
}

static int ubx_m10_i2c_get_supported_systems(const struct device *dev, gnss_systems_t *systems)
{
	ARG_UNUSED(dev);

	*systems = (GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS | GNSS_SYSTEM_GALILEO |
		    GNSS_SYSTEM_BEIDOU | GNSS_SYSTEM_SBAS | GNSS_SYSTEM_QZSS);
	return 0;
}

#endif /* GNSS_UBX_CONSTELLATION_CONFIG */

static int mon_ver_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			   size_t payload_len, void *user_data)
{
	const struct ubx_msg_mon_ver *ver = payload;
	uint8_t num_ext = (payload_len - sizeof(*ver)) / 30;

	LOG_INF("   SW: %s", ver->sw_version);
	LOG_DBG("   HW: %s", ver->hw_version);
	for (int i = 0; i < num_ext; i++) {
		LOG_DBG("EXT %d: %s", i, ver->extension[i].ext_version);
	}
	return 0;
}

/**
 * @brief Configure modem communications port
 *
 * Configures the modem to disable the serial port and only use UBX.
 * The data ready pin is enabled with the lowest threshold possible.
 */
static int ubx_m10_i2c_port_setup(const struct device *dev, bool hardware_reset)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	const struct ubx_m10_i2c_config *cfg = dev->config;
	struct ubx_m10_i2c_data *data = dev->data;
	int rc;

	if (!hardware_reset) {
		/* Clear any holdover configuration from BBR */
		struct ubx_msg_cfg_cfg_m10 cfg_cfg = {
			.clear_mask = UINT32_MAX,
		};

		ubx_msg_simple(&cfg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_CFG, &cfg_cfg,
			       sizeof(cfg_cfg));
		rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
		if (rc < 0) {
			LOG_WRN("Failed to reset previous configuration");
		}
	}

	/* First configuration message sets up the ports */
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_UART1_ENABLED, false);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_I2C_ENABLED, true);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_I2CINPROT_UBX, true);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_I2CINPROT_NMEA, false);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_I2COUTPROT_UBX, true);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_I2COUTPROT_NMEA, false);
	ubx_msg_finalise(&cfg_buf);
	rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		return rc;
	}

	/* Second configuration message configures the data ready pin */
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_ENABLED, true);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_PIN, cfg->common.data_ready_pio);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_POLARITY,
			     UBX_CFG_TXREADY_POLARITY_ACTIVE_LOW);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_INTERFACE,
			     UBX_CFG_TXREADY_INTERFACE_I2C);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_THRESHOLD, 1);

	/* We set the timepulse pin to use falling edges here, as the timepulse pin has a
	 * weak pullup to VCC internally. Using the rising edge default can result in spurious
	 * timepulse edges when the modem wakes from sleep modes:
	 *   https://portal.u-blox.com/s/question/0D52p00008HKD90CAH/spurious-time-pulses
	 */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TP_POL_TP1, UBX_CFG_TP_POL_TP1_FALLING_EDGE);

	/* Enable MON-RXR message */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_MON_RXR_I2C, 1);

	ubx_msg_finalise(&cfg_buf);
	rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		return rc;
	}
	/* GPIO data ready should be good at this point */
	modem_backend_ublox_i2c_use_data_ready_gpio(&data->i2c_backend);

	/* Display version information */
	return ubx_modem_send_sync_poll(&data->common.modem, UBX_MSG_CLASS_MON, UBX_MSG_ID_MON_VER,
					mon_ver_handler, NULL, SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m10_i2c_software_standby(const struct device *dev)
{
	UBX_MSG_BUF_DEFINE(pmreq, struct ubx_msg_rxm_pmreq);
	struct ubx_m10_i2c_data *data = dev->data;
	struct ubx_msg_rxm_pmreq payload = {
		.version = 0,
		.duration_ms = 0,
		.flags = UBX_MSG_RXM_PMREQ_FLAGS_BACKUP | UBX_MSG_RXM_PMREQ_FLAGS_FORCE,
		.wakeup_sources = UBX_MSG_RXM_PMREQ_WAKEUP_EXTINT0,
	};
	struct k_poll_event mon_rxr_events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 &data->common.mon_rxr_signal),
	};
	unsigned int signaled;
	int result;
	int rc;

	/* Reset previous events */
	k_poll_signal_reset(&data->common.mon_rxr_signal);

	/* Create request payload */
	ubx_msg_simple(&pmreq, UBX_MSG_CLASS_RXM, UBX_MSG_ID_RXM_PMREQ, &payload, sizeof(payload));

	/* We don't expect a response to this command */
	rc = ubx_modem_send_async(&data->common.modem, &pmreq, NULL, true);
	if (rc < 0) {
		LOG_WRN("SEND FAILED?");
		return rc;
	}

	/* Wait for the expected MON-RXR message */
	rc = k_poll(mon_rxr_events, ARRAY_SIZE(mon_rxr_events), SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		LOG_WRN("MON-RXR timeout");
		return rc;
	}
	k_poll_signal_check(&data->common.mon_rxr_signal, &signaled, &result);
	if (result & UBX_MSG_MON_RXR_AWAKE) {
		LOG_WRN("MON-RXR reported %s", "awake");
		return -EINVAL;
	}

	/* Modem takes some time to go to sleep and respond to wakeup requests */
	data->common.min_wake_time = K_TIMEOUT_ABS_MS(k_uptime_get() + 100);
	return 0;
}

static int ubx_m10_i2c_software_resume(const struct device *dev)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	struct ubx_m10_i2c_data *data = dev->data;
	int rc = 0;

	/* Wait until modem is ready to wake */
	k_sleep(data->common.min_wake_time);

	/* Wake by generating an edge on the EXTINT pin */
	ubx_common_extint_wake(dev);

	/* Ublox-M10 apparently does not output MON-RXR on wake, despite the M10 interface
	 * description saying it does.
	 */
#ifdef CONFIG_GNSS_U_BLOX_NO_API_COMPAT
	/* Validate we woke properly by running an arbitrary command with no effect */
	struct ubx_msg_cfg_cfg_m10 cfg_cfg = {0};

	ubx_msg_simple(&cfg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_CFG, &cfg_cfg, sizeof(cfg_cfg));
	rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		LOG_ERR("Failed to validate comms (%d)", rc);
	}
#else

	/* Modem uses NAV-PVT to fulfill requirements of GNSS API */
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_PVT_I2C, 1);
#ifdef CONFIG_GNSS_SATELLITES
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_SAT_I2C, 1);
#endif /* CONFIG_GNSS_SATELLITES */
	ubx_msg_finalise(&cfg_buf);
	rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		LOG_ERR("Failed to configure NAV-PVT rate (%d)", rc);
	}
#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */
	return rc;
}

static int ubx_m10_i2c_init(const struct device *dev)
{
	const struct ubx_m10_i2c_config *cfg = dev->config;
	struct ubx_m10_i2c_data *data = dev->data;
	struct modem_pipe *pipe;
	struct modem_backend_ublox_i2c_config i2c_backend_config = {
		.i2c = &cfg->i2c,
		.data_ready = &cfg->common.data_ready_gpio,
		.poll_period = K_MSEC(50),
	};

	/* Initialise modem backend */
	pipe = modem_backend_ublox_i2c_init(&data->i2c_backend, &i2c_backend_config);

	/* Run common initialisation logic*/
	return ubx_common_init(dev, pipe);
}

static const struct gnss_driver_api gnss_api = {
#ifndef CONFIG_GNSS_U_BLOX_NO_API_COMPAT
	.set_fix_rate = ubx_m10_i2c_set_fix_rate,
	.get_fix_rate = ubx_m10_i2c_get_fix_rate,
	.set_navigation_mode = ubx_m10_i2c_set_navigation_mode,
	.get_navigation_mode = ubx_m10_i2c_get_navigation_mode,
#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */
#ifdef GNSS_UBX_CONSTELLATION_CONFIG
	.set_enabled_systems = ubx_m10_i2c_set_enabled_systems,
	.get_enabled_systems = ubx_m10_i2c_get_enabled_systems,
	.get_supported_systems = ubx_m10_i2c_get_supported_systems,
#endif /* GNSS_UBX_CONSTELLATION_CONFIG */
	.get_latest_timepulse = ubx_common_get_latest_timepulse,
};

#define UBX_M10_I2C(inst)                                                                          \
	static const struct ubx_m10_i2c_config ubx_m10_cfg_##inst = {                              \
		.common = UBX_COMMON_CONFIG_INST(inst, ubx_m10_i2c_software_standby,               \
						 ubx_m10_i2c_software_resume,                      \
						 ubx_m10_i2c_port_setup),                          \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
	};                                                                                         \
	static struct ubx_m10_i2c_data ubx_m10_data_##inst;                                        \
	PM_DEVICE_DT_INST_DEFINE(inst, ubx_common_pm_control);                                     \
	I2C_DEVICE_DT_INST_DEFINE(inst, ubx_m10_i2c_init, PM_DEVICE_DT_INST_GET(inst),             \
				  &ubx_m10_data_##inst, &ubx_m10_cfg_##inst, POST_KERNEL,          \
				  CONFIG_GNSS_INIT_PRIORITY, &gnss_api);

DT_INST_FOREACH_STATUS_OKAY(UBX_M10_I2C)
