/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT u_blox_m8_emul

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/__assert.h>

#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/protocol.h>
#include <infuse/drivers/gnss/gnss_emul.h>

struct emul_config {
};

struct emul_data {
	struct ubx_modem_data modem_data;
	struct ubx_msg_nav_pvt current_pvt;
	struct k_timer navigation_timer;
	struct k_sem new_data;
	k_ticks_t timer_expiry;
	k_ticks_t latest_timepulse;
	gnss_systems_t systems;
	uint32_t fix_period;
	bool nav_pvt_enabled;
	bool nav_timegps_polled;
	int reset_cnt;
	int pm_rc;
	sys_slist_t handlers;
};

LOG_MODULE_REGISTER(ubx_m8_emul, LOG_LEVEL_INF);

static void message_dispatch(struct emul_data *data, uint8_t msg_class, uint8_t msg_id, void *msg,
			     size_t msg_len)
{
	struct ubx_message_handler_ctx *curr;
	struct ubx_message_handler_ctx *tmp;
	struct ubx_message_handler_ctx *prev = NULL;
	bool notify;
	int rc;

	/* Iterate over all pending message callbacks */
	prev = NULL;
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&data->handlers, curr, tmp, _node) {
		/* Notify if:
		 *   Handler class == UBX_MSG_CLASS_WILDCARD
		 * or:
		 *   Handler class == frame->class
		 *   and
		 *     Handler ID == UBX_MSG_ID_WILDCARD
		 *   or:
		 *     Handler ID == frame->id
		 */
		notify = (curr->message_class == UBX_MSG_CLASS_WILDCARD) ||
			 ((msg_class == curr->message_class) &&
			  ((msg_id == UBX_MSG_ID_WILDCARD) || (msg_id == curr->message_id)));

		if (notify) {
			/* Remove from handler list if a single response */
			if (curr->flags & UBX_HANDLING_RSP) {
				sys_slist_remove(&data->handlers, &prev->_node, &curr->_node);
			}
			/* Run callback */
			rc = curr->message_cb(msg_class, msg_id, msg, msg_len, curr->user_data);
			/* Raise signal if provided and no ACK/NAK is expected */
			if (curr->signal && (curr->flags & UBX_HANDLING_RSP)) {
				k_poll_signal_raise(curr->signal, rc);
			}
		}
		prev = curr;
	}
}

static void timer_fired(struct k_timer *timer)
{
	struct emul_data *data = CONTAINER_OF(timer, struct emul_data, navigation_timer);

	LOG_DBG("Navigation solution");

	data->timer_expiry = k_uptime_ticks();

	if (!data->nav_pvt_enabled) {
		return;
	}

	if (data->current_pvt.t_acc < (1 * NSEC_PER_MSEC)) {
		data->latest_timepulse = k_uptime_ticks();
	}

	if (data->nav_timegps_polled) {
		data->nav_timegps_polled = false;
		struct ubx_msg_nav_timegps timegps = {
			.itow = 100000,
			.ftow = 0,
			.week = 500,
			.leap_s = 21,
			.valid = data->current_pvt.t_acc < (2 * NSEC_PER_MSEC)
					 ? UBX_MSG_NAV_TIMEGPS_VALID_TOW_VALID |
						   UBX_MSG_NAV_TIMEGPS_VALID_WEEK_VALID
					 : 0,
			.t_acc = data->current_pvt.t_acc,
		};

		message_dispatch(data, UBX_MSG_CLASS_NAV, UBX_MSG_ID_NAV_TIMEGPS, &timegps,
				 sizeof(timegps));
	}

	message_dispatch(data, UBX_MSG_CLASS_NAV, UBX_MSG_ID_NAV_PVT, &data->current_pvt,
			 sizeof(data->current_pvt));
}

struct ubx_modem_data *ubx_modem_data_get(const struct device *dev)
{
	return dev->data;
}

void emul_gnss_pvt_configure(const struct device *dev, struct gnss_pvt_emul_location *emul_location)
{
	struct emul_data *data = dev->data;

	data->current_pvt.lat = emul_location->latitude;
	data->current_pvt.lon = emul_location->longitude;
	data->current_pvt.height = emul_location->height;
	data->current_pvt.h_acc = emul_location->h_acc;
	data->current_pvt.v_acc = emul_location->v_acc;
	data->current_pvt.t_acc = emul_location->t_acc;
	data->current_pvt.p_dop = emul_location->p_dop;
	data->current_pvt.num_sv = emul_location->num_sv;

	data->current_pvt.valid = emul_location->t_acc < (2 * NSEC_PER_MSEC)
					  ? UBX_MSG_NAV_PVT_VALID_DATE | UBX_MSG_NAV_PVT_VALID_TIME
					  : 0;
}

void emul_gnss_ubx_dev_ptrs(const struct device *dev, int **pm_rc, int **comms_reset_cnt)
{
	struct emul_data *data = dev->data;

	*pm_rc = &data->pm_rc;
	*comms_reset_cnt = &data->reset_cnt;
}

void ubx_modem_msg_subscribe(struct ubx_modem_data *modem,
			     struct ubx_message_handler_ctx *handler_ctx)
{
	struct emul_data *data = CONTAINER_OF(modem, struct emul_data, modem_data);

	LOG_INF("Subscribed to %02X:%02X", handler_ctx->message_class, handler_ctx->message_id);
	sys_slist_append(&data->handlers, &handler_ctx->_node);
}

void ubx_modem_msg_unsubscribe(struct ubx_modem_data *modem,
			       struct ubx_message_handler_ctx *handler_ctx)
{
	struct emul_data *data = CONTAINER_OF(modem, struct emul_data, modem_data);

	sys_slist_find_and_remove(&data->handlers, &handler_ctx->_node);
}

static struct ubx_frame *message_validate(struct net_buf_simple *buf)
{
	struct ubx_frame *frame;

	__ASSERT_NO_MSG(buf != NULL);
	frame = (void *)buf->data;
	__ASSERT_NO_MSG(frame->preamble_sync_char_1 == UBX_PREAMBLE_SYNC_CHAR_1);
	__ASSERT_NO_MSG(frame->preamble_sync_char_2 == UBX_PREAMBLE_SYNC_CHAR_2);

	return frame;
}

static void navigation_reschedule(struct emul_data *data)
{
	int64_t uptime = k_uptime_get();

	/* Generate solutions on the 0.6 second multiples */
	uptime -= uptime % 1000;
	uptime += 600;

	k_timer_start(&data->navigation_timer, K_TIMEOUT_ABS_MS(uptime), K_MSEC(data->fix_period));
}

int ubx_modem_send_sync_acked(struct ubx_modem_data *modem, struct net_buf_simple *buf,
			      k_timeout_t timeout)
{
	struct emul_data *data = CONTAINER_OF(modem, struct emul_data, modem_data);
	struct ubx_frame *frame = message_validate(buf);

	if ((frame->message_class == UBX_MSG_CLASS_CFG) &&
	    (frame->message_id == UBX_MSG_ID_CFG_RATE)) {
		struct ubx_msg_cfg_rate *cfg_rate = (void *)&frame->payload_and_checksum;

		if (cfg_rate->meas_rate % 1000 != 0) {
			LOG_ERR("Navigation rate must be multiple of 1000ms");
			return -EINVAL;
		}

		data->fix_period = cfg_rate->meas_rate * cfg_rate->nav_rate;
		LOG_INF("Navigation rate: %d ms", data->fix_period);
		navigation_reschedule(data);
	} else if ((frame->message_class == UBX_MSG_CLASS_CFG) &&
		   (frame->message_id == UBX_MSG_ID_CFG_MSG)) {
		struct ubx_msg_cfg_msg *cfg_msg = (void *)&frame->payload_and_checksum;

		if ((cfg_msg->msg_class != UBX_MSG_CLASS_NAV) ||
		    (cfg_msg->msg_id != UBX_MSG_ID_NAV_PVT)) {
			LOG_ERR("Unsupported MSG %02X:%02X", cfg_msg->msg_class, cfg_msg->msg_id);
			return -EINVAL;
		}
		data->nav_pvt_enabled = cfg_msg->rate == 1;
		LOG_INF("NAV-PVT: %s", data->nav_pvt_enabled ? "enabled" : "disabled");
	} else {
		return -EINVAL;
	}

	return 0;
}

int ubx_modem_send_async_poll(struct ubx_modem_data *modem, uint8_t message_class,
			      uint8_t message_id, uint8_t buf[8],
			      struct ubx_message_handler_ctx *handler_ctx)
{
	struct emul_data *data = CONTAINER_OF(modem, struct emul_data, modem_data);

	if ((message_class == UBX_MSG_CLASS_NAV) && (message_id == UBX_MSG_ID_NAV_TIMEGPS)) {
		data->nav_timegps_polled = true;
		/* Push handler onto queue */
		sys_slist_append(&data->handlers, &handler_ctx->_node);
	} else {
		LOG_ERR("Unsupported MSG %02X:%02X", message_class, message_id);
		return -EINVAL;
	}

	return 0;
}

static int emul_set_enabled_systems(const struct device *dev, gnss_systems_t systems)
{
	struct emul_data *data = dev->data;

	data->systems = systems;
	return 0;
}

static int emul_get_enabled_systems(const struct device *dev, gnss_systems_t *systems)
{
	struct emul_data *data = dev->data;

	*systems = data->systems;
	return 0;
}

static int emul_get_supported_systems(const struct device *dev, gnss_systems_t *systems)
{
	ARG_UNUSED(dev);

	*systems = (GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS | GNSS_SYSTEM_GALILEO |
		    GNSS_SYSTEM_BEIDOU | GNSS_SYSTEM_SBAS | GNSS_SYSTEM_QZSS);
	return 0;
}

static int emul_get_latest_timepulse(const struct device *dev, k_ticks_t *timestamp)
{
	struct emul_data *data = dev->data;

	k_ticks_t now = k_uptime_ticks();
	k_ticks_t max_age = (3 * CONFIG_SYS_CLOCK_TICKS_PER_SEC) / 2;
	k_ticks_t tp_age = now - data->latest_timepulse;

	if (tp_age > max_age) {
		/* Timepulse has not occurred in last 1.5 seconds, no longer valid */
		data->latest_timepulse = 0;
		return -EAGAIN;
	}
	*timestamp = data->latest_timepulse;
	return 0;
}

static int emul_pm_control(const struct device *dev, enum pm_device_action action)
{
	struct emul_data *data = dev->data;
	int rc = data->pm_rc;
	struct gnss_pvt_emul_location emul_loc = {
		0, 0, 0, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT16_MAX, 0,
	};

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		/* Reset state on resume */
		emul_gnss_pvt_configure(dev, &emul_loc);
		navigation_reschedule(data);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		k_timer_stop(&data->navigation_timer);
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		/* Default constellations */
		data->systems =
			GNSS_SYSTEM_GPS | GNSS_SYSTEM_GALILEO | GNSS_SYSTEM_QZSS | GNSS_SYSTEM_SBAS;
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		break;
	default:
		return -ENOTSUP;
	}

	data->pm_rc = 0;
	return rc;
}

int ubx_modem_comms_reset(const struct device *dev)
{
	struct emul_data *data = dev->data;

	data->reset_cnt += 1;
	return 0;
}

static int emul_init(const struct device *dev)
{
	struct emul_data *data = dev->data;

	data->fix_period = 1000;
	k_sem_init(&data->new_data, 0, 1);
	k_timer_init(&data->navigation_timer, timer_fired, NULL);

	return pm_device_driver_init(dev, emul_pm_control);
}

const struct gnss_driver_api emul_gnss_api = {
	.set_enabled_systems = emul_set_enabled_systems,
	.get_enabled_systems = emul_get_enabled_systems,
	.get_supported_systems = emul_get_supported_systems,
	.get_latest_timepulse = emul_get_latest_timepulse,
};

#define EMUL_INST(inst)                                                                            \
	static struct emul_data emul_drv_##inst;                                                   \
	static const struct emul_config emul_config_##inst = {};                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, emul_pm_control);                                           \
	DEVICE_DT_INST_DEFINE(inst, emul_init, PM_DEVICE_DT_INST_GET(inst), &emul_drv_##inst,      \
			      &emul_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,       \
			      &emul_gnss_api);

DT_INST_FOREACH_STATUS_OKAY(EMUL_INST);
