/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * Uses the NAV-PVT message to fulfill the requirements of the GNSS API.
 */

#define DT_DRV_COMPAT u_blox_m8_spi

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gnss/gnss_publish.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/modem/backend/u_blox_spi.h>
#include <infuse/gnss/ubx/defines.h>
#include <infuse/gnss/ubx/cfg.h>
#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/protocol.h>
#include <infuse/gnss/ubx/zephyr.h>

#include "u_blox_common.h"

#define SYNC_MESSAGE_TIMEOUT K_MSEC(250)

struct ubx_m8_spi_config {
	struct ubx_common_config common;
	struct spi_dt_spec spi;
};

struct ubx_m8_spi_data {
	struct ubx_common_data common;
	/* I2C modem backend */
	struct modem_backend_ublox_spi spi_backend;
};

LOG_MODULE_DECLARE(ubx_modem, CONFIG_GNSS_UBX_MODEM_LOG_LEVEL);

#ifndef CONFIG_GNSS_U_BLOX_NO_API_COMPAT

static int get_fix_rate_handler(uint8_t message_class, uint8_t message_id, const void *payload,
				size_t payload_len, void *user_data)
{
	const struct ubx_msg_cfg_rate *rate = payload;
	uint32_t *fix_interval_ms = user_data;

	/* Output interval is measurement period * solution ratio */
	*fix_interval_ms = (rate->meas_rate * rate->nav_rate);
	/* Valid if both params were returned */
	return (rate->meas_rate == 0) || (rate->nav_rate == 0) ? -EINVAL : 0;
}

static int ubx_m8_spi_get_fix_rate(const struct device *dev, uint32_t *fix_interval_ms)
{
	struct ubx_m8_spi_data *data = dev->data;

	return ubx_modem_send_sync_poll(&data->common.modem, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_RATE,
					get_fix_rate_handler, fix_interval_ms,
					SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m8_spi_set_fix_rate(const struct device *dev, uint32_t fix_interval_ms)
{
	NET_BUF_SIMPLE_DEFINE(msg_buf, 48);
	struct ubx_m8_spi_data *data = dev->data;
	const struct ubx_msg_cfg_rate cfg_rate = {
		.meas_rate = fix_interval_ms,
		.nav_rate = 1,
		.time_ref = UBX_MSG_CFG_RATE_TIME_REF_UTC,
	};

	ubx_msg_simple(&msg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_RATE, &cfg_rate,
		       sizeof(cfg_rate));
	return ubx_modem_send_sync_acked(&data->common.modem, &msg_buf, K_MSEC(250));
}

static int get_navigation_mode_handler(uint8_t message_class, uint8_t message_id,
				       const void *payload, size_t payload_len, void *user_data)
{
	const struct ubx_msg_cfg_nav5 *nav5 = payload;
	enum gnss_navigation_mode *mode = user_data;

	switch (nav5->dyn_model) {
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

static int ubx_m8_spi_get_navigation_mode(const struct device *dev, enum gnss_navigation_mode *mode)
{
	struct ubx_m8_spi_data *data = dev->data;

	return ubx_modem_send_sync_poll(&data->common.modem, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_NAV5,
					get_navigation_mode_handler, mode, SYNC_MESSAGE_TIMEOUT);
}

#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */

#ifdef GNSS_UBX_CONSTELLATION_CONFIG

static int cfg_gnss_copy(uint8_t message_class, uint8_t message_id, const void *payload,
			 size_t payload_len, void *user_data)
{
	struct net_buf_simple *buf = user_data;

	/* Copy message to user buffer */
	net_buf_simple_add_mem(buf, payload, payload_len);
	return 0;
}

static int ubx_m8_spi_set_enabled_systems(const struct device *dev, gnss_systems_t s)
{
	NET_BUF_SIMPLE_DEFINE(cfg_gnss, 96);
	struct ubx_m8_spi_data *data = dev->data;
	struct ubx_msg_cfg_gnss *gnss;
	const gnss_systems_t major =
		GNSS_SYSTEM_GPS | GNSS_SYSTEM_BEIDOU | GNSS_SYSTEM_GALILEO | GNSS_SYSTEM_GLONASS;
	int rc;

	if (s & GNSS_SYSTEM_GALILEO) {
		/* Configuring the M8 for Galileo comes with a host of other configuration
		 * requirements and commands.
		 */
		LOG_WRN("Galileo not supported");
		s ^= GNSS_SYSTEM_GALILEO;
	}
	if ((s & GNSS_SYSTEM_GPS) & !(s & GNSS_SYSTEM_QZSS)) {
		LOG_WRN("QZSS and GPS should be enabled together");
	}
	/* At least one major constellation must be enabled */
	if (!(s & major)) {
		return -EINVAL;
	}

	/* Reserve header space */
	ubx_msg_prepare(&cfg_gnss, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_GNSS);
	gnss = (void *)net_buf_simple_tail(&cfg_gnss);

	/* Get the current system configuration */
	rc = ubx_modem_send_sync_poll(&data->common.modem, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_GNSS,
				      cfg_gnss_copy, &cfg_gnss, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		return rc;
	}

	/* Set flags */
	for (int i = 0; i < gnss->num_cfg_blocks; i++) {
		uint8_t gnss_id = gnss->configs[i].gnss_id;
		uint32_t channels = gnss->configs[i].flags & ~UBX_MSG_CFG_GNSS_FLAGS_ENABLED;
		bool enabled = false;

		switch (gnss_id) {
		case UBX_GNSS_ID_GPS:
			enabled = s & GNSS_SYSTEM_GPS;
			break;
		case UBX_GNSS_ID_SBAS:
			enabled = s & GNSS_SYSTEM_SBAS;
			break;
		case UBX_GNSS_ID_GALILEO:
			enabled = s & GNSS_SYSTEM_GALILEO;
			break;
		case UBX_GNSS_ID_BEIDOU:
			enabled = s & GNSS_SYSTEM_BEIDOU;
			break;
		case UBX_GNSS_ID_QZSS:
			enabled = s & GNSS_SYSTEM_QZSS;
			break;
		case UBX_GNSS_ID_GLONASS:
			enabled = s & GNSS_SYSTEM_GLONASS;
			break;
		case UBX_GNSS_ID_NAVIC:
			enabled = s & GNSS_SYSTEM_IRNSS;
			break;
		}

		/* Set enabled bit to desired state */
		gnss->configs[i].flags = channels | (enabled ? UBX_MSG_CFG_GNSS_FLAGS_ENABLED : 0);
	}
	ubx_msg_finalise(&cfg_gnss);

	/* Update GNSS configuration */
	return ubx_modem_send_sync_acked(&data->common.modem, &cfg_gnss, K_SECONDS(1));
}

static int get_gnss_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			    size_t payload_len, void *user_data)
{
	const struct ubx_msg_cfg_gnss *gnss = payload;
	gnss_systems_t *systems = user_data;
	gnss_systems_t out = 0;

	for (int i = 0; i < gnss->num_cfg_blocks; i++) {
		uint8_t gnss_id = gnss->configs[i].gnss_id;
		uint32_t channels = gnss->configs[i].flags;

		/* Skip if enable bit not set */
		if (!(channels & UBX_MSG_CFG_GNSS_FLAGS_ENABLED)) {
			continue;
		}

		switch (gnss_id) {
		case UBX_GNSS_ID_GPS:
			out |= GNSS_SYSTEM_GPS;
			break;
		case UBX_GNSS_ID_SBAS:
			out |= GNSS_SYSTEM_SBAS;
			break;
		case UBX_GNSS_ID_GALILEO:
			out |= GNSS_SYSTEM_GALILEO;
			break;
		case UBX_GNSS_ID_BEIDOU:
			out |= GNSS_SYSTEM_BEIDOU;
			break;
		case UBX_GNSS_ID_QZSS:
			out |= GNSS_SYSTEM_QZSS;
			break;
		case UBX_GNSS_ID_GLONASS:
			out |= GNSS_SYSTEM_GLONASS;
			break;
		case UBX_GNSS_ID_NAVIC:
			out |= GNSS_SYSTEM_IRNSS;
			break;
		}
	}
	*systems = out;
	return 0;
}

static int ubx_m8_spi_get_enabled_systems(const struct device *dev, gnss_systems_t *systems)
{
	struct ubx_m8_spi_data *data = dev->data;

	return ubx_modem_send_sync_poll(&data->common.modem, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_GNSS,
					get_gnss_handler, systems, SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m8_spi_get_supported_systems(const struct device *dev, gnss_systems_t *systems)
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

static int ubx_m8_spi_port_setup(const struct device *dev, bool hardware_reset)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	const struct ubx_m8_spi_config *cfg = dev->config;
	struct ubx_m8_spi_data *data = dev->data;
	int rc;

	if (!hardware_reset) {
		/* Clear any holdover configuration from BBR and FLASH */
		struct ubx_msg_cfg_cfg_m8 cfg_cfg = {
			.clear_mask = UINT32_MAX,
			.device_mask = UBX_MSG_CFG_CFG_DEVICE_BBR | UBX_MSG_CFG_CFG_DEVICE_FLASH,
		};

		ubx_msg_simple(&cfg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_CFG, &cfg_cfg,
			       sizeof(cfg_cfg));
		rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
		if (rc < 0) {
			LOG_WRN("Failed to reset previous configuration");
		}
	}

	struct ubx_msg_cfg_prt_spi cfg_prt = {
		.port_id = UBX_MSG_CFG_PRT_PORT_ID_SPI,
		.tx_ready = UBX_MSG_CFG_PRT_TX_READY_EN | UBX_MSG_CFG_PRT_TX_READY_POL_ACTIVE_LOW |
			    UBX_MSG_CFG_PRT_TX_READY_CFG(cfg->common.data_ready_pio, 8),
		.mode = UBX_MSG_CFG_PRT_SPI_MODE_0,
		.in_proto_mask = UBX_MSG_CFG_PRT_PROTO_MASK_UBX,
		.out_proto_mask = UBX_MSG_CFG_PRT_PROTO_MASK_UBX,
		.flags = UBX_MSG_CFG_PRT_SPI_EXTENDED_TIMEOUT,
	};

	ubx_msg_simple(&cfg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_PRT, &cfg_prt, sizeof(cfg_prt));
	rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		return rc;
	}

	/* GPIO data ready should be good at this point */
	modem_backend_ublox_spi_use_data_ready_gpio(&data->spi_backend);

	/* Enable MON-RXR notifications */
	struct ubx_msg_cfg_msg cfg_rxr = {
		.msg_class = UBX_MSG_CLASS_MON,
		.msg_id = UBX_MSG_ID_MON_RXR,
		.rate = 1,
	};

	ubx_msg_simple(&cfg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_MSG, &cfg_rxr, sizeof(cfg_rxr));
	rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		return rc;
	}

	/* Backup port configuration in BBR */
	struct ubx_msg_cfg_cfg_m8 cfg_cfg = {
		.save_mask = UBX_MSG_CFG_CFG_MASK_IO_PORT | UBX_MSG_CFG_CFG_MASK_MSG_CONF,
		.device_mask = UBX_MSG_CFG_CFG_DEVICE_BBR | UBX_MSG_CFG_CFG_DEVICE_FLASH,
	};

	ubx_msg_simple(&cfg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_CFG, &cfg_cfg, sizeof(cfg_cfg));
	rc = ubx_modem_send_sync_acked(&data->common.modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		return rc;
	}

	/* Display version information */
	return ubx_modem_send_sync_poll(&data->common.modem, UBX_MSG_CLASS_MON, UBX_MSG_ID_MON_VER,
					mon_ver_handler, NULL, SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m8_spi_software_standby(const struct device *dev)
{
	UBX_MSG_BUF_DEFINE(pmreq, struct ubx_msg_rxm_pmreq);
	struct ubx_m8_spi_data *data = dev->data;
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

	/* We don't expect a response to the this command */
	rc = ubx_modem_send_async(&data->common.modem, &pmreq, NULL, true);
	if (rc < 0) {
		return rc;
	}

	/* Wait for the expected MON-RXR message */
	rc = k_poll(mon_rxr_events, ARRAY_SIZE(mon_rxr_events), SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		LOG_DBG("MON-RXR timeout");
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

static int ubx_m8_spi_software_resume(const struct device *dev)
{
	struct ubx_m8_spi_data *data = dev->data;
	struct k_poll_event mon_rxr_events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 &data->common.mon_rxr_signal),
	};
	unsigned int signaled;
	int result;
	int rc;

	/* Reset previous events */
	k_poll_signal_reset(&data->common.mon_rxr_signal);

	/* Wait until modem is ready to wake */
	k_sleep(data->common.min_wake_time);

	/* Wake by generating an edge on the EXTINT pin */
	ubx_common_extint_wake(dev);

	/* Wait for the expected MON-RXR message */
	rc = k_poll(mon_rxr_events, ARRAY_SIZE(mon_rxr_events), SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		LOG_DBG("MON-RXR timeout");
		return rc;
	}
	k_poll_signal_check(&data->common.mon_rxr_signal, &signaled, &result);
	if (!(result & UBX_MSG_MON_RXR_AWAKE)) {
		LOG_WRN("MON-RXR reported %s", "asleep");
		return -EINVAL;
	}

#ifndef CONFIG_GNSS_U_BLOX_NO_API_COMPAT
	NET_BUF_SIMPLE_DEFINE(msg_buf, 32);
	const struct ubx_msg_cfg_msg nav_pvt = {
		.msg_class = UBX_MSG_CLASS_NAV,
		.msg_id = UBX_MSG_ID_NAV_PVT,
		.rate = 1,
	};

	/* Enabled NAV-PVT to fulfill requirements of GNSS API */
	ubx_msg_simple(&msg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_MSG, &nav_pvt, sizeof(nav_pvt));
	rc = ubx_modem_send_sync_acked(&data->common.modem, &msg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		LOG_ERR("Failed to configure NAV-PVT rate (%d)", rc);
		return rc;
	}
#ifdef CONFIG_GNSS_SATELLITES
	const struct ubx_msg_cfg_msg nav_sat = {
		.msg_class = UBX_MSG_CLASS_NAV,
		.msg_id = UBX_MSG_ID_NAV_SAT,
		.rate = 1,
	};

	/* Enabled NAV-PVT to fulfill requirements of GNSS API */
	ubx_msg_simple(&msg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_MSG, &nav_sat, sizeof(nav_sat));
	rc = ubx_modem_send_sync_acked(&data->common.modem, &msg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		LOG_ERR("Failed to configure NAV-SAT rate (%d)", rc);
		return rc;
	}
#endif /* CONFIG_GNSS_SATELLITES */
#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */
	return 0;
}

static void ubx_m8_fifo_poll(const struct device *dev)
{
	struct ubx_m8_spi_data *data = dev->data;

	/* Schedule the FIFO data query */
	k_work_reschedule(&data->spi_backend.common.fifo_read, K_NO_WAIT);
}

static int ubx_m8_spi_init(const struct device *dev)
{
	const struct ubx_m8_spi_config *cfg = dev->config;
	struct ubx_m8_spi_data *data = dev->data;
	struct modem_pipe *pipe;
	struct modem_backend_ublox_spi_config spi_backend_config = {
		.spi = &cfg->spi,
		.data_ready = &cfg->common.data_ready_gpio,
		.poll_period = K_MSEC(50),
	};

	/* Initialise modem backend */
	pipe = modem_backend_ublox_spi_init(&data->spi_backend, &spi_backend_config);

	/* Run common initialisation logic*/
	return ubx_common_init(dev, pipe);
}

static const struct gnss_driver_api gnss_api = {
#ifndef CONFIG_GNSS_U_BLOX_NO_API_COMPAT
	.set_fix_rate = ubx_m8_spi_set_fix_rate,
	.get_fix_rate = ubx_m8_spi_get_fix_rate,
	.get_navigation_mode = ubx_m8_spi_get_navigation_mode,
#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */
#ifdef GNSS_UBX_CONSTELLATION_CONFIG
	.set_enabled_systems = ubx_m8_spi_set_enabled_systems,
	.get_enabled_systems = ubx_m8_spi_get_enabled_systems,
	.get_supported_systems = ubx_m8_spi_get_supported_systems,
#endif /* GNSS_UBX_CONSTELLATION_CONFIG */
	.get_latest_timepulse = ubx_common_get_latest_timepulse,
};

#define UBX_M8_SPI(inst)                                                                           \
	static const struct ubx_m8_spi_config ubx_m8_cfg_##inst = {                                \
		.common = UBX_COMMON_CONFIG_INST(inst, ubx_m8_spi_software_standby,                \
						 ubx_m8_spi_software_resume,                       \
						 ubx_m8_spi_port_setup, ubx_m8_fifo_poll),         \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),          \
	};                                                                                         \
	static struct ubx_m8_spi_data ubx_m8_data_##inst;                                          \
	PM_DEVICE_DT_INST_DEFINE(inst, ubx_common_pm_control);                                     \
	I2C_DEVICE_DT_INST_DEFINE(inst, ubx_m8_spi_init, PM_DEVICE_DT_INST_GET(inst),              \
				  &ubx_m8_data_##inst, &ubx_m8_cfg_##inst, POST_KERNEL,            \
				  CONFIG_GNSS_INIT_PRIORITY, &gnss_api);

DT_INST_FOREACH_STATUS_OKAY(UBX_M8_SPI)
