/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/sys/util.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gnss/gnss_publish.h>
#include <zephyr/logging/log.h>

#include <infuse/gnss/ubx/defines.h>
#include <infuse/gnss/ubx/zephyr.h>

#include "u_blox_common.h"

#define MIN_TIMEPULSE_EDGES 3

static const char *const ubx_gnss_names[] = {
	[UBX_GNSS_ID_GPS] = "GPS",         [UBX_GNSS_ID_SBAS] = "SBAS",
	[UBX_GNSS_ID_GALILEO] = "GALILEO", [UBX_GNSS_ID_BEIDOU] = "BEIDOU",
	[UBX_GNSS_ID_NAVIC] = "NAVIC",     [UBX_GNSS_ID_QZSS] = "QZSS",
	[UBX_GNSS_ID_GLONASS] = "GLONASS",
};

LOG_MODULE_DECLARE(ubx_modem);

int ubx_gnss_id_to_gnss_system(enum ubx_gnss_id gnss_id)
{
	switch (gnss_id) {
	case UBX_GNSS_ID_GPS:
		return GNSS_SYSTEM_GPS;
	case UBX_GNSS_ID_SBAS:
		return GNSS_SYSTEM_SBAS;
	case UBX_GNSS_ID_GALILEO:
		return GNSS_SYSTEM_GALILEO;
	case UBX_GNSS_ID_BEIDOU:
		return GNSS_SYSTEM_BEIDOU;
	case UBX_GNSS_ID_QZSS:
		return GNSS_SYSTEM_QZSS;
	case UBX_GNSS_ID_GLONASS:
		return GNSS_SYSTEM_GLONASS;
	case UBX_GNSS_ID_NAVIC:
		return GNSS_SYSTEM_IRNSS;
	default:
		return -EINVAL;
	}
}

int gnss_system_to_ubx_gnss_id(enum gnss_system gnss_system)
{
	switch (gnss_system) {
	case GNSS_SYSTEM_GPS:
		return UBX_GNSS_ID_GPS;
	case GNSS_SYSTEM_SBAS:
		return UBX_GNSS_ID_SBAS;
	case GNSS_SYSTEM_GALILEO:
		return UBX_GNSS_ID_GALILEO;
	case GNSS_SYSTEM_BEIDOU:
		return UBX_GNSS_ID_BEIDOU;
	case GNSS_SYSTEM_QZSS:
		return UBX_GNSS_ID_QZSS;
	case GNSS_SYSTEM_GLONASS:
		return UBX_GNSS_ID_GLONASS;
	case GNSS_SYSTEM_IRNSS:
		return UBX_GNSS_ID_NAVIC;
	default:
		return -EINVAL;
	}
}

const char *ubx_gnss_id_name(enum ubx_gnss_id gnss_id)
{
	if (gnss_id > ARRAY_SIZE(ubx_gnss_names)) {
		return "N/A";
	}
	return ubx_gnss_names[gnss_id];
}

#ifndef CONFIG_GNSS_U_BLOX_NO_API_COMPAT

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
	struct gnss_satellite satellites[CONFIG_GNSS_U_BLOX_SATELLITES_COUNT];
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
#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */

void ubx_common_extint_wake(const struct device *dev)
{
	const struct ubx_common_config *cfg = dev->config;

	gpio_pin_set_dt(&cfg->extint_gpio, 1);
	k_sleep(K_MSEC(1));
	gpio_pin_set_dt(&cfg->extint_gpio, 0);
	/* Modem needs some time before it is ready to respond to commands */
	k_sleep(K_MSEC(250));
}

int ubx_common_get_latest_timepulse(const struct device *dev, k_ticks_t *timestamp)
{
	const struct ubx_common_config *cfg = dev->config;
	struct ubx_common_data *data = dev->data;
	k_ticks_t now = k_uptime_ticks();
	k_ticks_t max_age = (3 * CONFIG_SYS_CLOCK_TICKS_PER_SEC) / 2;
	k_ticks_t tp_age = now - data->latest_timepulse;

	if (cfg->timepulse_gpio.port == NULL) {
		/* No timepulse pin connected */
		return -ENOTSUP;
	}
	if (data->latest_timepulse <= MIN_TIMEPULSE_EDGES) {
		/* Timepulse has not occurred or is not considered stable */
		return -EAGAIN;
	}
	if (tp_age > max_age) {
		/* Timepulse has not occurred in last 1.5 seconds, no longer valid */
		data->latest_timepulse = 0;
		return -EAGAIN;
	}
	*timestamp = data->latest_timepulse;
	return 0;
}

static void timepulse_gpio_callback(const struct device *dev, struct gpio_callback *cb,
				    uint32_t pins)
{
	struct ubx_common_data *data = CONTAINER_OF(cb, struct ubx_common_data, timepulse_cb);
	k_ticks_t now = k_uptime_ticks();

	if (data->latest_timepulse < MIN_TIMEPULSE_EDGES) {
		data->latest_timepulse += 1;
	} else {
		data->latest_timepulse = now;
	}
	LOG_DBG("%lld", data->latest_timepulse);
}

static int mon_rxr_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			   size_t payload_len, void *user_data)
{
	const struct ubx_msg_mon_rxr *mon_rxr = payload;
	struct k_poll_signal *sig = user_data;

	LOG_DBG("MON-RXR: %02X", mon_rxr->flags);
	return k_poll_signal_raise(sig, mon_rxr->flags);
}

int ubx_common_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct ubx_common_config *cfg = dev->config;
	struct ubx_common_data *data = dev->data;
	bool hardware_reset = false;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		shared_device_release_dt(&cfg->ant_switch);
		/* Disable timepulse interrupt */
		if (cfg->timepulse_gpio.port != NULL) {
			data->latest_timepulse = 0;
			(void)gpio_pin_interrupt_configure_dt(&cfg->timepulse_gpio,
							      GPIO_INT_DISABLE);
		}
		/* Put into low power mode */
		rc = cfg->pm_funcs.software_standby(dev);
		if (rc < 0) {
			LOG_INF("Failed to go to standby mode");
			return rc;
		}
		/* Notify modem layer */
		ubx_modem_software_standby(&data->modem);
		break;
	case PM_DEVICE_ACTION_RESUME:
		rc = cfg->pm_funcs.software_resume(dev);
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
		shared_device_request_dt(&cfg->ant_switch);
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		(void)modem_pipe_close(data->modem.pipe, K_SECONDS(2));
		gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_DISCONNECTED);
		gpio_pin_configure_dt(&cfg->extint_gpio, GPIO_DISCONNECTED);
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		gpio_pin_configure_dt(&cfg->extint_gpio, GPIO_OUTPUT_INACTIVE);
		gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);

		/* Attempt to wake modem in case it is in sleep state */
		ubx_common_extint_wake(dev);
		/* Attempt to communicate without hardware reset to preserve GNSS state */
		rc = modem_pipe_open(data->modem.pipe, K_SECONDS(2));
		if (rc < 0) {
			/* Failed to open, hardware reset and try again */
			LOG_INF("Resetting %s...", dev->name);
			hardware_reset = true;
			gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
			k_sleep(K_MSEC(2));
			gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);
			rc = modem_pipe_open(data->modem.pipe, K_SECONDS(2));
		}
		if (rc < 0) {
			LOG_WRN("Failed to establish comms");
			return rc;
		}
		/* Configure modem for comms */
		rc = cfg->pm_funcs.port_setup(dev, hardware_reset);
		if (rc < 0) {
			LOG_INF("Failed to setup comms port");
			modem_pipe_close(data->modem.pipe, K_SECONDS(2));
			return rc;
		}
		/* Put into low power mode */
		rc = cfg->pm_funcs.software_standby(dev);
		if (rc < 0) {
			LOG_INF("Failed to go to standby mode");
			modem_pipe_close(data->modem.pipe, K_SECONDS(2));
			return rc;
		}
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

int ubx_common_init(const struct device *dev, struct modem_pipe *pipe)
{
	const struct ubx_common_config *cfg = dev->config;
	struct ubx_common_data *data = dev->data;

	ubx_modem_init(&data->modem, pipe);

	/* Permanently handle MON-RXR messages */
	data->mon_rxr_handler = (struct ubx_message_handler_ctx){
		.message_class = UBX_MSG_CLASS_MON,
		.message_id = UBX_MSG_ID_MON_RXR,
		.message_cb = mon_rxr_handler,
		.user_data = &data->mon_rxr_signal,
	};
	k_poll_signal_init(&data->mon_rxr_signal);
	ubx_modem_msg_subscribe(&data->modem, &data->mon_rxr_handler);

	/* Setup timepulse pin interrupt */
	if (cfg->timepulse_gpio.port != NULL) {
		(void)gpio_pin_configure_dt(&cfg->timepulse_gpio, GPIO_INPUT);
		gpio_init_callback(&data->timepulse_cb, timepulse_gpio_callback,
				   BIT(cfg->timepulse_gpio.pin));
		if (gpio_add_callback_dt(&cfg->timepulse_gpio, &data->timepulse_cb) < 0) {
			LOG_ERR("Unable to add timepulse callback");
		}
	}

	if (!shared_device_is_ready_dt(&cfg->ant_switch)) {
		LOG_WRN("RF switch not ready");
	}

#ifndef CONFIG_GNSS_U_BLOX_NO_API_COMPAT
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
#endif /* CONFIG_GNSS_U_BLOX_NO_API_COMPAT */

	/* Run boot sequence */
	return pm_device_driver_init(dev, ubx_common_pm_control);
}

int ubx_modem_comms_reset(const struct device *dev)
{
	enum pm_device_state current_state;

	(void)pm_device_state_get(dev, &current_state);
	if (current_state != PM_DEVICE_STATE_SUSPENDED) {
		return -EAGAIN;
	}
	/* Unitialise resources */
	(void)ubx_common_pm_control(dev, PM_DEVICE_ACTION_TURN_OFF);
	/* Wait a short duration */
	k_sleep(K_MSEC(100));
	/* Attempt to re-establish communications */
	return ubx_common_pm_control(dev, PM_DEVICE_ACTION_TURN_ON);
}

struct ubx_modem_data *ubx_modem_data_get(const struct device *dev)
{
	struct ubx_common_data *data = dev->data;

	return &data->modem;
}
