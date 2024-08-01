/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * Uses the NAV-PVT message to fulfill the requirements of the GNSS API.
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gnss/gnss_publish.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/modem/backend/u_blox_i2c.h>
#include <infuse/gnss/ubx/defines.h>
#include <infuse/gnss/ubx/cfg.h>
#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/protocol.h>
#include <infuse/gnss/ubx/zephyr.h>

#define DT_DRV_COMPAT u_blox_m10_i2c

#define SYNC_MESSAGE_TIMEOUT K_MSEC(250)

struct ubx_m10_i2c_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec extint_gpio;
	struct gpio_dt_spec timepulse_gpio;
	struct gpio_dt_spec data_ready_gpio;
	uint8_t data_ready_pio;
};

struct ubx_m10_i2c_data {
	/* UBX modem data */
	struct ubx_modem_data modem;
	/* I2C modem backend */
	struct modem_backend_ublox_i2c i2c_backend;
	/* Wake time */
	k_timeout_t min_wake_time;
	/* Callback for data ready GPIO */
	struct gpio_callback timepulse_cb;
	/* Timestamp of the latest timepulse */
	k_ticks_t latest_timepulse;
#ifdef CONFIG_GNSS_U_BLOX_M10_API_COMPAT
	/* NAV-PVT message handler */
	struct ubx_message_handler_ctx pvt_handler;
#ifdef CONFIG_GNSS_SATELLITES
	/* NAV-SAT message handler */
	struct ubx_message_handler_ctx sat_handler;
#endif /* CONFIG_GNSS_SATELLITES */
#endif /* CONFIG_GNSS_U_BLOX_M10_API_COMPAT */
};

BUILD_ASSERT(__alignof(struct ubx_frame) == 1);

LOG_MODULE_REGISTER(ublox_m10, CONFIG_U_BLOX_M10_I2C_LOG_LEVEL);

#ifdef CONFIG_GNSS_U_BLOX_M10_API_COMPAT

static int nav_pvt_cb(uint8_t message_class, uint8_t message_id, const void *payload,
		      size_t payload_len, void *user_data)
{
	const struct device *dev = user_data;
	const struct ubx_msg_nav_pvt *pvt = payload;
	/* Translate to GNSS API structure */
	struct gnss_data data = {
		.nav_data =
			{
				.latitude = ((int64_t)pvt->lat) * 100,
				.longitude = ((int64_t)pvt->lon) * 100,
				.bearing = pvt->head_mot / 10,
				.speed = pvt->g_speed,
				.altitude = pvt->height,
			},
		.info =
			{
				.satellites_cnt = pvt->num_sv,
				.hdop = pvt->p_dop * 10,
				.fix_status = ubx_nav_pvt_to_fix_status(pvt),
				.fix_quality = ubx_nav_pvt_to_fix_quality(pvt),
			},
		.utc =
			{
				.century_year = pvt->year % 100,
				.month = pvt->month,
				.month_day = pvt->day,
				.hour = pvt->hour,
				.minute = pvt->min,
				.millisecond = (1000 * (uint16_t)pvt->sec) + (pvt->nano / 1000000),
			},
	};
	/* Push data to compile-time consumers */
	gnss_publish_data(dev, &data);
	return 0;
}

#ifdef CONFIG_GNSS_SATELLITES

static int nav_sat_cb(uint8_t message_class, uint8_t message_id, const void *payload,
		      size_t payload_len, void *user_data)
{
	const struct device *dev = user_data;
	const struct ubx_msg_nav_sat *sat = payload;
	const struct ubx_msg_nav_sat_sv *sv;
	struct gnss_satellite satellites[CONFIG_GNSS_U_BLOX_M10_I2C_SATELLITES_COUNT];
	uint8_t num_report = 0;
	uint32_t sv_quality;
	bool tracked;

	for (int i = 0; i < sat->num_svs; i++) {
		sv = &sat->svs[i];
		sv_quality = sv->flags & UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_MASK;
		tracked = (sv_quality == UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_ACQUIRED) ||
			  (sv_quality >= UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_CODE_LOCKED);

		LOG_DBG("\t%7s ID:%3d Qual: %d CNo: %2ddBHz Elev: %3ddeg Azim: %3ddeg %08X",
			ubx_gnss_id_name(sv->gnss_id), sv->sv_id,
			sv->flags & UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_MASK, sv->cno, sv->elev,
			sv->azim, sv->flags);

		if (num_report >= ARRAY_SIZE(satellites)) {
			continue;
		}
		/* Untracked satellites already skipped */
		satellites[num_report].system = ubx_gnss_id_to_gnss_system(sv->gnss_id);
		satellites[num_report].prn = sv->sv_id;
		satellites[num_report].snr = sv->cno;
		satellites[num_report].azimuth = sv->azim;
		satellites[num_report].elevation = sv->elev;
		satellites[num_report].is_tracked = tracked;
		num_report += 1;
	}
	/* Push data to compile-time consumers */
	gnss_publish_satellites(dev, satellites, num_report);
	return 0;
}

#endif /* CONFIG_GNSS_SATELLITES */

static int get_fix_rate_handler(uint8_t message_class, uint8_t message_id, const void *payload,
				size_t payload_len, void *user_data)
{
	const struct ubx_msg_cfg_valget_response *valget = payload;
	uint32_t *fix_interval_ms = user_data;
	const uint8_t *val_ptr = valget->cfg_data;
	size_t val_len = payload_len - sizeof(*valget);
	uint16_t meas = 0, nav = 0;
	struct ubx_cfg_val cfg_val;

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

	return ubx_modem_send_sync(&data->modem, &cfg_buf, UBX_HANDLING_RSP_ACK,
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
	return ubx_modem_send_sync_acked(&data->modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
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

	return ubx_modem_send_sync(&data->modem, &cfg_buf, UBX_HANDLING_RSP_ACK,
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
	return ubx_modem_send_sync_acked(&data->modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
}

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

	return ubx_modem_send_sync(&data->modem, &cfg_buf, UBX_HANDLING_RSP_ACK,
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
	rc = ubx_modem_send_sync_acked(&data->modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
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

#endif /* CONFIG_GNSS_U_BLOX_M10_API_COMPAT */

static int ubx_m10_i2c_get_latest_timepulse(const struct device *dev, k_ticks_t *timestamp)
{
	const struct ubx_m10_i2c_config *cfg = dev->config;
	struct ubx_m10_i2c_data *data = dev->data;

	if (cfg->timepulse_gpio.port == NULL) {
		/* No timepulse pin connected */
		return -ENOTSUP;
	}
	if (data->latest_timepulse == 0) {
		/* Timepulse interrupt has not occurred yet */
		return -EAGAIN;
	}
	*timestamp = data->latest_timepulse;
	return 0;
}

static void timepulse_gpio_callback(const struct device *dev, struct gpio_callback *cb,
				    uint32_t pins)
{
	struct ubx_m10_i2c_data *data = CONTAINER_OF(cb, struct ubx_m10_i2c_data, timepulse_cb);

	data->latest_timepulse = k_uptime_ticks();
	LOG_DBG("");
}

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
static int ubx_m10_i2c_port_setup(const struct device *dev)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	const struct ubx_m10_i2c_config *cfg = dev->config;
	struct ubx_m10_i2c_data *data = dev->data;
	int rc;

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
	rc = ubx_modem_send_sync_acked(&data->modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		return rc;
	}

	/* Second configuration message configures the data ready pin */
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_ENABLED, true);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_PIN, cfg->data_ready_pio);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_POLARITY,
			     UBX_CFG_TXREADY_POLARITY_ACTIVE_HIGH);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_INTERFACE,
			     UBX_CFG_TXREADY_INTERFACE_I2C);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TXREADY_THRESHOLD, 1);
	ubx_msg_finalise(&cfg_buf);
	rc = ubx_modem_send_sync_acked(&data->modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
	if (rc < 0) {
		return rc;
	}
	/* GPIO data ready should be good at this point */
	modem_backend_ublox_i2c_use_data_ready_gpio(&data->i2c_backend);

	/* Display version information */
	return ubx_modem_send_sync_poll(&data->modem, UBX_MSG_CLASS_MON, UBX_MSG_ID_MON_VER,
					mon_ver_handler, NULL, SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m10_i2c_software_standby(const struct device *dev)
{
	UBX_MSG_BUF_DEFINE(pmreq, struct ubx_msg_rxm_pmreq);
	struct ubx_m10_i2c_data *data = dev->data;
	struct ubx_msg_rxm_pmreq *payload;

	/* Create request payload */
	ubx_msg_prepare(&pmreq, UBX_MSG_CLASS_RXM, UBX_MSG_ID_RXM_PMREQ);
	payload = net_buf_simple_add(&pmreq, sizeof(*payload));
	*payload = (struct ubx_msg_rxm_pmreq){
		.version = 0,
		.duration_ms = 0,
		.flags = UBX_MSG_RXM_PMREQ_FLAGS_BACKUP | UBX_MSG_RXM_PMREQ_FLAGS_FORCE,
		.wakeup_sources = UBX_MSG_RXM_PMREQ_WAKEUP_EXTINT0,
	};
	ubx_msg_finalise(&pmreq);

	/* Modem takes some time to go to sleep and respond to wakeup requests */
	data->min_wake_time = K_TIMEOUT_ABS_MS(k_uptime_get() + 10);

	/* We don't expect a response, need to wait for TX to finish */
	return ubx_modem_send_async(&data->modem, &pmreq, NULL, true);
}

static int ubx_m10_i2c_software_resume(const struct device *dev)
{
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 64);
	const struct ubx_m10_i2c_config *cfg = dev->config;
	struct ubx_m10_i2c_data *data = dev->data;

	/* Wait until modem is ready to wake */
	k_sleep(data->min_wake_time);
	/* Wake by generating an edge on the EXTINT pin */
	gpio_pin_set_dt(&cfg->extint_gpio, 1);
	k_sleep(K_MSEC(1));
	gpio_pin_set_dt(&cfg->extint_gpio, 0);
	/* Modem needs some time before it is ready to respond to commands */
	k_sleep(K_MSEC(250));
	/* Modem uses NAV-PVT to fulfill requirements of GNSS API */
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_PVT_I2C, 1);
#ifdef CONFIG_GNSS_SATELLITES
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_SAT_I2C, 1);
#endif /* CONFIG_GNSS_SATELLITES */
	ubx_msg_finalise(&cfg_buf);
	return ubx_modem_send_sync_acked(&data->modem, &cfg_buf, SYNC_MESSAGE_TIMEOUT);
}

static int ubx_m10_i2c_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct ubx_m10_i2c_config *cfg = dev->config;
	struct ubx_m10_i2c_data *data = dev->data;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		/* Disable timepulse interrupt */
		if (cfg->timepulse_gpio.port != NULL) {
			data->latest_timepulse = 0;
			(void)gpio_pin_interrupt_configure_dt(&cfg->timepulse_gpio,
							      GPIO_INT_DISABLE);
		}
		/* Put into low power mode */
		rc = ubx_m10_i2c_software_standby(dev);
		if (rc < 0) {
			LOG_INF("Failed to go to standby mode");
			return rc;
		}
		/* Notify modem layer */
		ubx_modem_software_standby(&data->modem);
		break;
	case PM_DEVICE_ACTION_RESUME:
		rc = ubx_m10_i2c_software_resume(dev);
		if (rc < 0) {
			LOG_INF("Failed to resume");
			return rc;
		}
		/* Enable timepulse interrupt */
		if (cfg->timepulse_gpio.port != NULL) {
			data->latest_timepulse = 0;
			(void)gpio_pin_interrupt_configure_dt(&cfg->timepulse_gpio,
							      GPIO_INT_EDGE_TO_ACTIVE);
		}
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_DISCONNECTED);
		gpio_pin_configure_dt(&cfg->extint_gpio, GPIO_DISCONNECTED);
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		LOG_DBG("Resetting %s...", dev->name);
		gpio_pin_configure_dt(&cfg->extint_gpio, GPIO_OUTPUT_INACTIVE);
		gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
		k_sleep(K_MSEC(2));
		gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);

		/* Open the pipe synchronously */
		rc = modem_pipe_open(data->modem.pipe);
		if (rc < 0) {
			LOG_INF("Failed to establish comms");
			return rc;
		}
		/* Configure modem for I2C comms */
		rc = ubx_m10_i2c_port_setup(dev);
		if (rc < 0) {
			LOG_INF("Failed to setup comms port");
			return rc;
		}
		/* Put into low power mode */
		rc = ubx_m10_i2c_software_standby(dev);
		if (rc < 0) {
			LOG_INF("Failed to go to standby mode");
			return rc;
		}
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int ubx_m10_i2c_init(const struct device *dev)
{
	const struct ubx_m10_i2c_config *cfg = dev->config;
	struct ubx_m10_i2c_data *data = dev->data;
	struct modem_pipe *pipe;
	struct modem_backend_ublox_i2c_config i2c_backend_config = {
		.i2c = &cfg->i2c,
		.data_ready = &cfg->data_ready_gpio,
		.poll_period = K_MSEC(50),
	};

	/* Initialise modem backend */
	pipe = modem_backend_ublox_i2c_init(&data->i2c_backend, &i2c_backend_config);
	ubx_modem_init(&data->modem, pipe);

	/* Setup timepulse pin interrupt */
	if (cfg->timepulse_gpio.port != NULL) {
		(void)gpio_pin_configure_dt(&cfg->timepulse_gpio, GPIO_INPUT);
		gpio_init_callback(&data->timepulse_cb, timepulse_gpio_callback,
				   BIT(cfg->timepulse_gpio.pin));
		if (gpio_add_callback(cfg->timepulse_gpio.port, &data->timepulse_cb) < 0) {
			LOG_ERR("Unable to add timepulse callback");
		}
	}

#ifdef CONFIG_GNSS_U_BLOX_M10_API_COMPAT
	/* Subscribe to all NAV-PVT messages */
	data->pvt_handler.message_class = UBX_MSG_CLASS_NAV,
	data->pvt_handler.message_id = UBX_MSG_ID_NAV_PVT,
	data->pvt_handler.message_cb = nav_pvt_cb;
	data->pvt_handler.user_data = (void *)dev;
	ubx_modem_msg_subscribe(&data->modem, &data->pvt_handler);

#ifdef CONFIG_GNSS_SATELLITES
	/* Subscribe to all NAV-SAT messages */
	data->sat_handler.message_class = UBX_MSG_CLASS_NAV,
	data->sat_handler.message_id = UBX_MSG_ID_NAV_SAT,
	data->sat_handler.message_cb = nav_sat_cb;
	data->sat_handler.user_data = (void *)dev;
	ubx_modem_msg_subscribe(&data->modem, &data->sat_handler);
#endif /* CONFIG_GNSS_SATELLITES */
#endif /* CONFIG_GNSS_U_BLOX_M10_API_COMPAT */

	/* Run boot sequence */
	return pm_device_driver_init(dev, ubx_m10_i2c_pm_control);
}

struct ubx_modem_data *ubx_modem_data_get(const struct device *dev)
{
	struct ubx_m10_i2c_data *data = dev->data;

	return &data->modem;
}

static const struct gnss_driver_api gnss_api = {
#ifdef CONFIG_GNSS_U_BLOX_M10_API_COMPAT
	.set_fix_rate = ubx_m10_i2c_set_fix_rate,
	.get_fix_rate = ubx_m10_i2c_get_fix_rate,
	.set_navigation_mode = ubx_m10_i2c_set_navigation_mode,
	.get_navigation_mode = ubx_m10_i2c_get_navigation_mode,
	.set_enabled_systems = ubx_m10_i2c_set_enabled_systems,
	.get_enabled_systems = ubx_m10_i2c_get_enabled_systems,
	.get_supported_systems = ubx_m10_i2c_get_supported_systems,
#endif /* CONFIG_GNSS_U_BLOX_M10_API_COMPAT */
	.get_latest_timepulse = ubx_m10_i2c_get_latest_timepulse,
};

#define UBX_M10_I2C(inst)                                                                          \
	static const struct ubx_m10_i2c_config ubx_m10_cfg_##inst = {                              \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                            \
		.extint_gpio = GPIO_DT_SPEC_INST_GET(inst, extint_gpios),                          \
		.timepulse_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, timepulse_gpios, {0}),            \
		.data_ready_gpio = GPIO_DT_SPEC_INST_GET(inst, data_ready_gpios),                  \
		.data_ready_pio = DT_INST_PROP(inst, data_ready_pio),                              \
	};                                                                                         \
	static struct ubx_m10_i2c_data ubx_m10_data_##inst;                                        \
	PM_DEVICE_DT_INST_DEFINE(inst, ubx_m10_i2c_pm_control);                                    \
	I2C_DEVICE_DT_INST_DEFINE(inst, ubx_m10_i2c_init, PM_DEVICE_DT_INST_GET(inst),             \
				  &ubx_m10_data_##inst, &ubx_m10_cfg_##inst, POST_KERNEL,          \
				  CONFIG_GNSS_INIT_PRIORITY, &gnss_api);

DT_INST_FOREACH_STATUS_OKAY(UBX_M10_I2C)
