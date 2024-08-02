/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/gnss.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/gnss.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/cfg.h>
#include <infuse/gnss/ubx/protocol.h>

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_LOCATION);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION)

enum {
	TIME_SYNC_RUNNING = BIT(0),
	TIME_SYNC_DONE = BIT(1),
};

struct gnss_run_state {
	const struct device *dev;
	struct ubx_modem_data *modem;
	const struct task_schedule *schedule;
	struct ubx_message_handler_ctx timegps;
	struct ubx_msg_nav_pvt best_fix;
	struct k_poll_signal alive;
	uint64_t next_time_sync;
	uint8_t flags;
};

/* Expecting these two struct to be equivalent for logging purposes */
BUILD_ASSERT(sizeof(struct ubx_msg_nav_pvt) == sizeof(struct tdf_ubx_nav_pvt));

LOG_MODULE_REGISTER(task_gnss, CONFIG_TASK_GNSS_UBX_LOG_LEVEL);

static int nav_timegps_cb(uint8_t message_class, uint8_t message_id, const void *payload,
			  size_t payload_len, void *user_data)
{
	const struct ubx_msg_nav_timegps *timegps = payload;
	struct gnss_run_state *state = user_data;
	uint8_t time_validity =
		UBX_MSG_NAV_TIMEGPS_VALID_TOW_VALID | UBX_MSG_NAV_TIMEGPS_VALID_WEEK_VALID;
	bool time_valid = (timegps->valid & time_validity) == time_validity;
	k_ticks_t timepulse;
	int rc;

	/* Clear running flag */
	state->flags &= ~TIME_SYNC_RUNNING;

	LOG_DBG("NAV-TIMEGPS: (%d.%d) Acc: %d Valid: %02X: Leap: %d", timegps->week, timegps->itow,
		timegps->t_acc, timegps->valid, timegps->leap_s);

	/* Exit if GPS time knowledge is not valid */
	if (!time_valid) {
		return 0;
	}

	/* Ensure math below is well behaved */
	if (timegps->itow <= 500) {
		return 0;
	}

	/* Merge iTOW and fTOW as per Interface Description */
	uint64_t weektime_us = (1000 * (uint64_t)timegps->itow) + (timegps->ftow / 1000);
	uint64_t subsec_us = weektime_us % 1000000;
	uint32_t week_seconds = weektime_us / 1000000;
	uint32_t subsec_infuse = (65536 * subsec_us) / 1000000;
	uint64_t epoch_time = epoch_time_from_gps(timegps->week, week_seconds, subsec_infuse);

	/* If there is no current time knowledge, do a quick sync ASAP */
	if (!epoch_time_trusted_source(epoch_time_get_source(), true)) {
		struct timeutil_sync_instant sync = {
			.local = k_uptime_ticks(),
			.ref = epoch_time,
		};

		LOG_INF("%s time sync @ GPS (%d.%d)", "Coarse", timegps->week, timegps->itow);
		(void)epoch_time_set_reference(TIME_SOURCE_GNSS, &sync);
	}

	/* Minimum time accuracy accepted for fine sync (ns) */
	if (timegps->t_acc >= 1000) {
		return 0;
	}

	/* Fine sync requires valid timepulse */
	rc = gnss_get_latest_timepulse(state->dev, &timepulse);
	if (rc != 0) {
		return 0;
	}

	struct timeutil_sync_instant sync = {
		.local = timepulse,
		.ref = epoch_time,
	};

	LOG_INF("%s time sync @ GPS (%d.%d)", "Fine", timegps->week, timegps->itow);
	/* Notify time library of sync */
	(void)epoch_time_set_reference(TIME_SOURCE_GNSS, &sync);
	state->flags |= TIME_SYNC_DONE;
	state->next_time_sync =
		k_uptime_get() + (CONFIG_TASK_RUNNER_GNSS_TIME_RESYNC_PERIOD_SEC * MSEC_PER_SEC);
	return 0;
}

static void log_and_publish(struct gnss_run_state *state, const struct ubx_msg_nav_pvt *pvt)
{
	struct tdf_gcs_wgs84_llha llha = {
		.location =
			{
				.latitude = pvt->lat,
				.longitude = pvt->lon,
				.height = pvt->height,
			},
		.h_acc = pvt->h_acc,
		.v_acc = pvt->v_acc,
	};
	k_ticks_t timepulse;
	uint64_t epoch_time;

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, &llha, K_FOREVER);

	/* Timestamp from timepulse if available */
	if (gnss_get_latest_timepulse(state->dev, &timepulse) == 0) {
		epoch_time = epoch_time_from_ticks(timepulse);
	} else {
		epoch_time = epoch_time_now();
	}
	/* Log output */
	task_schedule_tdf_log(state->schedule, TASK_GNSS_LOG_LLHA, TDF_GCS_WGS84_LLHA,
			      sizeof(struct tdf_gcs_wgs84_llha), epoch_time, &llha);
	task_schedule_tdf_log(state->schedule, TASK_GNSS_LOG_UBX_NAV_PVT, TDF_UBX_NAV_PVT,
			      sizeof(struct tdf_ubx_nav_pvt), epoch_time, pvt);
}

static int nav_pvt_cb(uint8_t message_class, uint8_t message_id, const void *payload,
		      size_t payload_len, void *user_data)
{
	const struct ubx_msg_nav_pvt *pvt = payload;
	struct gnss_run_state *state = user_data;
	const struct task_gnss_args *args = &state->schedule->task_args.infuse.gnss;
	uint8_t run_target = (args->flags & TASK_GNSS_FLAGS_RUN_MASK);
	uint8_t time_validity = UBX_MSG_NAV_PVT_VALID_DATE | UBX_MSG_NAV_PVT_VALID_TIME;
	bool valid_time = (pvt->valid & time_validity) == time_validity;
	bool valid_hacc = (pvt->h_acc <= (1000 * (uint32_t)args->accuracy_m));
	bool valid_pdop = (pvt->p_dop <= (10 * (uint32_t)args->position_dop));
	bool not_running = !(state->flags & TIME_SYNC_RUNNING);

	/* Periodically print fix state */
	if (k_uptime_seconds() % 30 == 0) {
		LOG_INF("NAV-PVT: Lat: %9d Lon: %9d HAcc: %umm VAcc: %umm pDOP: %d NumSV: %d",
			pvt->lat, pvt->lon, pvt->h_acc, pvt->v_acc, pvt->p_dop / 100, pvt->num_sv);
	} else {
		LOG_DBG("NAV-PVT: Lat: %9d Lon: %9d HAcc: %umm VAcc: %umm pDOP: %d NumSV: %d",
			pvt->lat, pvt->lon, pvt->h_acc, pvt->v_acc, pvt->p_dop / 100, pvt->num_sv);
	}

	if (run_target == TASK_GNSS_FLAGS_RUN_FOREVER) {
		/* If running perpetually, log each output */
		log_and_publish(state, pvt);
	} else if (run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) {
		/* If running for a fix, update best location fix */
		if (pvt->h_acc <= state->best_fix.h_acc) {
			memcpy(&state->best_fix, pvt, sizeof(*pvt));
		}
	}

	if (valid_time && not_running && (k_uptime_get() >= state->next_time_sync)) {
		static uint8_t msg_buf[8];
		/* Not yet time synced, modem has general idea of time.
		 * Query NAV-TIMEGPS directly to determine GPS time validity.
		 * The response to this query will come on the next navigation solution.
		 */

		static NET_BUF_SIMPLE_DEFINE(poll_req, 16);
		ubx_msg_prepare(&poll_req, UBX_MSG_CLASS_NAV, UBX_MSG_ID_NAV_TIMEGPS);
		ubx_msg_finalise(&poll_req);

		state->timegps.flags = UBX_HANDLING_RSP;
		state->timegps.message_class = UBX_MSG_CLASS_NAV;
		state->timegps.message_class = UBX_MSG_ID_NAV_TIMEGPS;
		state->timegps.message_cb = nav_timegps_cb;
		state->timegps.user_data = state;

		LOG_DBG("Querying NAV-TIMEGPS");
		state->flags |= TIME_SYNC_RUNNING;
		ubx_modem_send_async_poll(state->modem, UBX_MSG_CLASS_NAV, UBX_MSG_ID_NAV_TIMEGPS,
					  msg_buf, &state->timegps);
	}

	if ((run_target == TASK_GNSS_FLAGS_RUN_TO_TIME_SYNC) && (state->flags & TIME_SYNC_DONE)) {
		/* Time sync done, terminate */
		LOG_INF("Terminating due to %s", "time sync complete");
		k_poll_signal_raise(&state->alive, 1);
	} else if ((run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) &&
		   (state->flags & TIME_SYNC_DONE) && valid_hacc && valid_pdop) {
		/* Location fix done, terminate */
		LOG_INF("Terminating due to %s", "fix obtained");
		k_poll_signal_raise(&state->alive, 1);
	} else {
		/* Notify task thread we are still going */
		k_poll_signal_raise(&state->alive, 0);
	}

	return 0;
}

void gnss_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate,
		  void *gnss_dev)
{
	const struct device *gnss = gnss_dev;
	const struct task_gnss_args *args = &schedule->task_args.infuse.gnss;
	uint8_t run_target = (args->flags & TASK_GNSS_FLAGS_RUN_MASK);
	struct gnss_run_state run_state = {0};
	struct ubx_message_handler_ctx pvt_handler_ctx = {
		.message_class = UBX_MSG_CLASS_NAV,
		.message_id = UBX_MSG_ID_NAV_PVT,
		.message_cb = nav_pvt_cb,
		.user_data = &run_state,
	};
	int rc;

	run_state.dev = gnss;
	run_state.modem = ubx_modem_data_get(gnss);
	run_state.schedule = schedule;
	run_state.best_fix.h_acc = UINT32_MAX;
	k_poll_signal_init(&run_state.alive);

	/* Request sensor to be powered */
	rc = pm_device_runtime_get(gnss);
	if (rc < 0) {
		k_sleep(K_SECONDS(1));
		LOG_ERR("Terminating due to %s", "PM failure");
		return;
	}

	NET_BUF_SIMPLE_DEFINE(cfg_buf, 48);
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	/* Core location message */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_PVT_I2C, 1);
	/* Power mode configuration */
	if (args->flags & TASK_GNSS_FLAGS_PERFORMANCE_MODE) {
		/* Normal mode tracking (default values) */
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_NAVSPG_OUTFIL_PACC, 100);
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_NAVSPG_OUTFIL_PDOP, 250);
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_PM_OPERATEMODE,
				     UBX_CFG_KEY_PM_OPERATEMODE_FULL);
	} else {
		/* Cyclic Tracking, entering POT ASAP, no acquisition timeout */
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_NAVSPG_OUTFIL_PACC, args->accuracy_m);
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_NAVSPG_OUTFIL_PDOP, args->position_dop);
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_PM_ONTIME, 0);
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_PM_UPDATEEPH, true);
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_PM_DONOTENTEROFF, true);
		UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_PM_OPERATEMODE,
				     UBX_CFG_KEY_PM_OPERATEMODE_PSMCT);
	}
	ubx_msg_finalise(&cfg_buf);
	rc = ubx_modem_send_sync_acked(run_state.modem, &cfg_buf, K_MSEC(250));

	/* Subscribe to NAV-PVT message */
	ubx_modem_msg_subscribe(run_state.modem, &pvt_handler_ctx);

	/* Block until runner requests termination (all work happens in NAV-PVT callback) */
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, terminate),
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 &run_state.alive),
	};
	int signaled, result;

	while (1) {
		/* Block on the NAV-PVT callback and Task Runner requests */
		if (k_poll(events, ARRAY_SIZE(events), K_SECONDS(2)) == -EAGAIN) {
			LOG_WRN("Terminating due to %s", "callback timeout");
			break;
		}
		k_poll_signal_check(terminate, &signaled, &result);
		if (signaled) {
			LOG_INF("Terminating due to %s", "runner request");
			break;
		}
		k_poll_signal_check(&run_state.alive, &signaled, &result);
		if (signaled) {
			k_poll_signal_reset(&run_state.alive);
			if (result == 1) {
				break;
			}
		}
	}

	/* Log at end if run for a location fix */
	if (run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) {
		struct ubx_msg_nav_pvt *best = &run_state.best_fix;

		LOG_INF("Final Location: Lat %9d Lon %9d Height %dm Acc %dcm", best->lat, best->lon,
			best->height / 1000, best->h_acc / 10);
		log_and_publish(&run_state, &run_state.best_fix);
	}

	/* Cleanup message subscription */
	ubx_modem_msg_unsubscribe(run_state.modem, &pvt_handler_ctx);

	/* Release power requirement */
	rc = pm_device_runtime_put(gnss);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Terminate thread */
}
