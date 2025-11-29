/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
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

#include "gnss_common.h"

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_LOCATION);
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_UBX_NAV_PVT);
#define ZBUS_CHAN     INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION)
#define ZBUS_CHAN_PVT INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_UBX_NAV_PVT)

#define TIME_VALID_FLAGS                                                                           \
	(UBX_MSG_NAV_TIMEGPS_VALID_TOW_VALID | UBX_MSG_NAV_TIMEGPS_VALID_WEEK_VALID)

enum {
	TIME_SYNC_RUNNING = BIT(0),
	TIME_SYNC_DONE = BIT(1),
};

struct gnss_run_state {
	const struct device *dev;
	struct ubx_modem_data *modem;
	const struct task_schedule *schedule;
	struct ubx_message_handler_ctx timegps;
#ifdef CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO
	struct ubx_message_handler_ctx nav_sat;
#endif /* CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO */
	struct ubx_msg_nav_pvt latest_pvt;
	struct ubx_msg_nav_pvt best_fix;
	struct ubx_msg_nav_timegps latest_timegps;
	struct k_poll_signal nav_pvt_rx;
	struct k_poll_signal nav_timegps_rx;
	struct gnss_fix_timeout_state timeout_state;
	uint64_t next_time_sync;
	uint32_t task_start;
	uint32_t time_acquired;
	uint8_t flags;
};

/* Expecting these two struct to be equivalent for logging purposes */
BUILD_ASSERT(sizeof(struct ubx_msg_nav_pvt) == sizeof(struct tdf_ubx_nav_pvt));

BUILD_ASSERT(IS_ENABLED(CONFIG_GNSS_UBX_M8) + IS_ENABLED(CONFIG_GNSS_UBX_M10) == 1,
	     "Expected exactly one of CONFIG_GNSS_UBX_M8 and CONFIG_GNSS_UBX_M10 to be enabled");

LOG_MODULE_REGISTER(task_gnss, CONFIG_TASK_GNSS_LOG_LEVEL);

static int nav_timegps_cb(uint8_t message_class, uint8_t message_id, const void *payload,
			  size_t payload_len, void *user_data)
{
	const struct ubx_msg_nav_timegps *timegps = payload;
	struct gnss_run_state *state = user_data;

	/* Copy payload to state */
	memcpy(&state->latest_timegps, timegps, sizeof(*timegps));
	/* Notify task thread new NAV-TIMEGPS message is available */
	k_poll_signal_raise(&state->nav_timegps_rx, 0);

	return 0;
}

static void nav_timegps_handle(struct gnss_run_state *state, const struct task_gnss_args *args)
{
	const struct ubx_msg_nav_timegps *timegps = &state->latest_timegps;
	bool time_valid = (timegps->valid & TIME_VALID_FLAGS) == TIME_VALID_FLAGS;
	k_ticks_t timepulse;
	int rc;

	/* Clear running flag */
	state->flags &= ~TIME_SYNC_RUNNING;

	LOG_DBG("NAV-TIMEGPS: (%d.%d) Acc: %d Valid: %02X: Leap: %d", timegps->week, timegps->itow,
		timegps->t_acc, timegps->valid, timegps->leap_s);

	/* Exit if GPS time knowledge is not valid */
	if (!time_valid) {
		return;
	}

	/* Ensure math below is well behaved (ftow can be negative) */
	if (timegps->itow <= 500) {
		return;
	}

	/* Merge iTOW and fTOW as per Interface Description */
	uint64_t weektime_us = (1000 * (uint64_t)timegps->itow) + (timegps->ftow / 1000);
	uint64_t subsec_us = weektime_us % 1000000;
	uint32_t week_seconds = weektime_us / 1000000;
	uint32_t subsec_infuse = (65536 * subsec_us) / 1000000;
	uint64_t epoch_time = epoch_time_from_gps(timegps->week, week_seconds, subsec_infuse);

	/* If there is no current time knowledge, or it is old enough, do a quick sync ASAP */
	if (epoch_time_reference_age() > CONFIG_TASK_RUNNER_GNSS_TIME_COARSE_SYNC_PERIOD_SEC) {
		struct timeutil_sync_instant sync = {
			.local = k_uptime_ticks(),
			.ref = epoch_time,
		};

		LOG_INF("%s time sync @ GPS (%d.%d)", "Coarse", timegps->week, timegps->itow);
		(void)epoch_time_set_reference(TIME_SOURCE_GNSS, &sync);
	}

	/* Minimum time accuracy accepted for fine sync (ns) */
	if (timegps->t_acc >= 1000) {
		return;
	}

	/* Fine sync requires valid timepulse */
	rc = gnss_get_latest_timepulse(state->dev, &timepulse);
	if (rc != 0) {
		return;
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
	state->time_acquired = k_uptime_seconds();
}

static uint64_t log_and_publish(struct gnss_run_state *state, const struct ubx_msg_nav_pvt *pvt)
{
	const struct tdf_ubx_nav_pvt *tdf_pvt = (const void *)pvt;
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

	/* Set known values on invalid accuracies */
	if (pvt->h_acc >= INT32_MAX) {
		llha.h_acc = INT32_MAX;
		llha.v_acc = INT32_MAX;
	}
	/* Set invalid location on insufficient accuracy */
	if (pvt->h_acc > (CONFIG_TASK_RUNNER_GNSS_MINIMUM_ACCURACY_M * 1000)) {
		llha.location.longitude = -1810000000;
		llha.location.latitude = -910000000;
		llha.location.height = 0;
	}

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, &llha, K_FOREVER);
	zbus_chan_pub(ZBUS_CHAN_PVT, tdf_pvt, K_FOREVER);

	/* Timestamp from timepulse if available */
	if (gnss_get_latest_timepulse(state->dev, &timepulse) == 0) {
		epoch_time = epoch_time_from_ticks(timepulse);
	} else {
		epoch_time = epoch_time_now();
	}
	/* Log output */
	TASK_SCHEDULE_TDF_LOG(state->schedule, TASK_GNSS_LOG_LLHA, TDF_GCS_WGS84_LLHA, epoch_time,
			      &llha);
	TASK_SCHEDULE_TDF_LOG(state->schedule, TASK_GNSS_LOG_PVT, TDF_UBX_NAV_PVT, epoch_time,
			      tdf_pvt);

	return epoch_time;
}

static int nav_pvt_cb(uint8_t message_class, uint8_t message_id, const void *payload,
		      size_t payload_len, void *user_data)
{
	const struct ubx_msg_nav_pvt *pvt = payload;
	struct gnss_run_state *state = user_data;

	/* Copy payload to state */
	memcpy(&state->latest_pvt, pvt, sizeof(*pvt));
	/* Notify task thread new NAV-PVT message is available */
	k_poll_signal_raise(&state->nav_pvt_rx, 0);

	return 0;
}

/* Returns true when task should terminate */
static bool nav_pvt_handle(struct gnss_run_state *state, const struct task_gnss_args *args)
{
	const struct ubx_msg_nav_pvt *pvt = &state->latest_pvt;
	uint8_t run_target = (args->flags & TASK_GNSS_FLAGS_RUN_MASK);
	uint8_t time_validity = UBX_MSG_NAV_PVT_VALID_DATE | UBX_MSG_NAV_PVT_VALID_TIME;
	bool valid_time = (pvt->valid & time_validity) == time_validity;
	bool valid_hacc = (pvt->h_acc <= (1000 * (uint32_t)args->accuracy_m));
	bool valid_pdop = (pvt->p_dop <= (10 * (uint32_t)args->position_dop));
	bool not_running = !(state->flags & TIME_SYNC_RUNNING);
	uint32_t runtime = k_uptime_seconds() - state->task_start;

	/* Periodically print fix state */
	if (k_uptime_seconds() % 30 == 0) {
		LOG_INF("NAV-PVT: Lat: %9d Lon: %9d Height: %6d", pvt->lat, pvt->lon, pvt->height);
		LOG_INF("         HAcc: %umm VAcc: %umm pDOP: %d NumSV: %d", pvt->h_acc, pvt->v_acc,
			pvt->p_dop / 100, pvt->num_sv);
	} else {
		LOG_DBG("NAV-PVT: Lat: %9d Lon: %9d Height: %6d", pvt->lat, pvt->lon, pvt->height);
		LOG_DBG("         HAcc: %umm VAcc: %umm pDOP: %d NumSV: %d", pvt->h_acc, pvt->v_acc,
			pvt->p_dop / 100, pvt->num_sv);
	}

	if (run_target == TASK_GNSS_FLAGS_RUN_FOREVER) {
		/* If running perpetually, log each output */
		log_and_publish(state, pvt);
	} else if (run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) {
		/* If running for a fix, update best location fix */
		if (pvt->h_acc <= state->best_fix.h_acc) {
			memcpy(&state->best_fix, pvt, sizeof(*pvt));
		}
		/* Check if the fix has timed out */
		if (gnss_run_to_fix_timeout(args, &state->timeout_state, pvt->h_acc, runtime)) {
			return true;
		}
	}

	if (valid_time && not_running && (k_uptime_get() >= state->next_time_sync)) {
		static uint8_t msg_buf[8];
		/* Not yet time synced, modem has general idea of time.
		 * Query NAV-TIMEGPS directly to determine GPS time validity.
		 * The response to this query will come on the next navigation solution.
		 */
		state->timegps.flags = UBX_HANDLING_RSP;
		state->timegps.message_class = UBX_MSG_CLASS_NAV;
		state->timegps.message_id = UBX_MSG_ID_NAV_TIMEGPS;
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
		return true;
	} else if ((run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) &&
		   (state->flags & TIME_SYNC_DONE) && valid_hacc && valid_pdop) {
		/* Location fix done, terminate */
		LOG_INF("Terminating due to %s", "fix obtained");
		return true;
	}

	/* Continue fix */
	return false;
}

#ifdef CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO
static int nav_sat_cb(uint8_t message_class, uint8_t message_id, const void *payload,
		      size_t payload_len, void *user_data)
{
	const struct ubx_msg_nav_sat *sat = payload;
	bool info = k_uptime_seconds() % 30 == 0;

	for (int i = 0; i < sat->num_svs; i++) {
		uint8_t quality = sat->svs[i].flags & UBX_MSG_NAV_SAT_FLAGS_QUALITY_IND_MASK;
		uint8_t used = !!(sat->svs[i].flags & UBX_MSG_NAV_SAT_FLAGS_SV_USED);

		if (info) {
			LOG_INF("\tGNSS: %d ID: %3d CNo: %3d dB/Hz Qual: %d Used: %d",
				sat->svs[i].gnss_id, sat->svs[i].sv_id, sat->svs[i].cno, quality,
				used);
		} else {
			LOG_DBG("\tGNSS: %d ID: %3d CNo: %3d dB/Hz Qual: %d Used: %d",
				sat->svs[i].gnss_id, sat->svs[i].sv_id, sat->svs[i].cno, quality,
				used);
		}
	}
	return 0;
}
#endif /* CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO */

static void gnss_configure(const struct device *gnss, const struct task_gnss_args *args)
{
	struct ubx_modem_data *modem = ubx_modem_data_get(gnss);
	gnss_systems_t constellations;
	uint8_t dynamics;
	int rc;

	/* Constellation configuration if requested */
	if (args->constellations) {
		rc = gnss_set_enabled_systems(gnss, args->constellations);
		if (rc != 0) {
			LOG_WRN("Failed to configure constellations %02X (%d)",
				args->constellations, rc);
		}
	}
	/* Output supported and enabled constellations */
	if (gnss_get_supported_systems(gnss, &constellations) == 0) {
		LOG_INF("Constellations: %02X (%s)", constellations, "supported");
	}
	if (gnss_get_enabled_systems(gnss, &constellations) == 0) {
		LOG_INF("Constellations: %02X (%s)", constellations, "enabled");
	}

	if (args->flags & TASK_GNSS_FLAGS_PERFORMANCE_MODE) {
		LOG_INF("Mode: Performance");
	} else {
		LOG_INF("Mode: Low Power (Accuracy %d m, PDOP %d)", args->accuracy_m,
			args->position_dop / 10);
	}

	/* Dynamic model */
	dynamics = args->dynamic_model;
	if ((dynamics == 1) || (dynamics > 12)) {
		/* Unknown dynamics platform */
		LOG_WRN("Unknown dynamics (%d), reverting to PORTABLE", dynamics);
		dynamics = UBX_CFG_NAVSPG_DYNMODEL_PORTABLE;
	} else {
		LOG_INF("Dynamic model: %d", dynamics);
	}

#ifdef CONFIG_GNSS_UBX_M10
	NET_BUF_SIMPLE_DEFINE(cfg_buf, 50);
	ubx_msg_prepare_valset(&cfg_buf,
			       UBX_MSG_CFG_VALSET_LAYERS_RAM | UBX_MSG_CFG_VALSET_LAYERS_BBR);
	/* Core location message */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_PVT_I2C, 1);
#ifdef CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO
	/* Satellite information message */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_MSGOUT_UBX_NAV_SAT_I2C, 1);
#endif /* CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO */
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
	/* Align timepulse to GPS time */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_TP_TIMEGRID_TP1, UBX_CFG_TP_TIMEGRID_TP1_GPS);
	/* Platform dynamics */
	UBX_CFG_VALUE_APPEND(&cfg_buf, UBX_CFG_KEY_NAVSPG_DYNMODEL, dynamics);

	ubx_msg_finalise(&cfg_buf);
	rc = ubx_modem_send_sync_acked(modem, &cfg_buf, K_MSEC(250));
	if (rc < 0) {
		LOG_WRN("Failed to configure modem");
	}
#endif /* CONFIG_GNSS_UBX_M10 */

#ifdef CONFIG_GNSS_UBX_M8
	NET_BUF_SIMPLE_DEFINE(msg_buf, 48);
	const struct ubx_msg_cfg_rate cfg_rate = {
		.meas_rate = 1000,
		.nav_rate = 1,
		.time_ref = UBX_MSG_CFG_RATE_TIME_REF_GPS,
	};
	const struct ubx_msg_cfg_msg cfg_msg = {
		.msg_class = UBX_MSG_CLASS_NAV,
		.msg_id = UBX_MSG_ID_NAV_PVT,
		.rate = 1,
	};

	ubx_msg_simple(&msg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_RATE, &cfg_rate,
		       sizeof(cfg_rate));
	rc = ubx_modem_send_sync_acked(modem, &msg_buf, K_MSEC(250));
	if (rc < 0) {
		LOG_WRN("Failed to configure navigation rate");
	}
	ubx_msg_simple(&msg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_MSG, &cfg_msg, sizeof(cfg_msg));
	rc = ubx_modem_send_sync_acked(modem, &msg_buf, K_MSEC(250));
	if (rc < 0) {
		LOG_WRN("Failed to configure NAV-PVT rate");
	}

#ifdef CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO
	const struct ubx_msg_cfg_msg cfg_msg_sat = {
		.msg_class = UBX_MSG_CLASS_NAV,
		.msg_id = UBX_MSG_ID_NAV_SAT,
		.rate = 1,
	};

	ubx_msg_simple(&msg_buf, UBX_MSG_CLASS_CFG, UBX_MSG_ID_CFG_RATE, &cfg_msg_sat,
		       sizeof(cfg_msg_sat));
	rc = ubx_modem_send_sync_acked(modem, &msg_buf, K_MSEC(250));
	if (rc < 0) {
		LOG_WRN("Failed to configure navigation rate");
	}
#endif /* CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO */
#endif /* CONFIG_GNSS_UBX_M8 */
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
	run_state.task_start = k_uptime_seconds();
	run_state.time_acquired = 0;
	gnss_timeout_reset(&run_state.timeout_state);
	k_poll_signal_init(&run_state.nav_pvt_rx);
	k_poll_signal_init(&run_state.nav_timegps_rx);

	LOG_DBG("Starting");

	/* Request sensor to be powered */
	rc = pm_device_runtime_get(gnss);
	if ((rc < 0) && pm_device_is_powered(gnss)) {
		/* Device is in software shutdown mode, try to recover communications */
		LOG_WRN("Failed to request PM, resetting comms");
		rc = ubx_modem_comms_reset(gnss);
		if (rc == 0) {
			/* Communications recovered, try PM again */
			rc = pm_device_runtime_get(gnss);
		}
	}
	if (rc < 0) {
		k_sleep(K_SECONDS(1));
		LOG_ERR("Terminating due to %s", "PM failure");
		return;
	}

	/* Configure the modem according to the arguments */
	gnss_configure(gnss, args);

	/* Subscribe to NAV-PVT message */
	ubx_modem_msg_subscribe(run_state.modem, &pvt_handler_ctx);

#ifdef CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO
	struct ubx_message_handler_ctx sat_handler_ctx = {
		.message_class = UBX_MSG_CLASS_NAV,
		.message_id = UBX_MSG_ID_NAV_SAT,
		.message_cb = nav_sat_cb,
		.user_data = NULL,
	};

	/* Subscribe to NAV-SAT message */
	ubx_modem_msg_subscribe(run_state.modem, &sat_handler_ctx);
#endif /* CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO */

	/* Block until runner requests termination (all work happens in NAV-PVT callback) */
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, terminate),
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 &run_state.nav_pvt_rx),
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 &run_state.nav_timegps_rx),
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
		k_poll_signal_check(&run_state.nav_pvt_rx, &signaled, &result);
		if (signaled) {
			k_poll_signal_reset(&run_state.nav_pvt_rx);
			if (nav_pvt_handle(&run_state, args)) {
				break;
			}
		}
		k_poll_signal_check(&run_state.nav_timegps_rx, &signaled, &result);
		if (signaled) {
			k_poll_signal_reset(&run_state.nav_timegps_rx);
			nav_timegps_handle(&run_state, args);
		}
	}

	/* Log at end of run for a location fix */
	if (run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) {
		struct ubx_msg_nav_pvt *best = &run_state.best_fix;
		struct tdf_gnss_fix_info fix_info = {
			.time_fix = run_state.time_acquired
					    ? (run_state.time_acquired - run_state.task_start)
					    : UINT16_MAX,
			.location_fix = k_uptime_seconds() - run_state.task_start,
			.num_sv = run_state.best_fix.num_sv,
		};
		uint64_t epoch_time;

		LOG_INF("Final Location: Lat %9d Lon %9d Height %dm Acc %dcm", best->lat, best->lon,
			best->height / 1000, best->h_acc / 10);
		epoch_time = log_and_publish(&run_state, &run_state.best_fix);

		/* Log fix information */
		TASK_SCHEDULE_TDF_LOG(schedule, TASK_GNSS_LOG_FIX_INFO, TDF_GNSS_FIX_INFO,
				      epoch_time, &fix_info);
	}

	/* NAV-TIMEGPS message could have been requested and not yet received */
	ubx_modem_msg_unsubscribe(run_state.modem, &run_state.timegps);

	/* Cleanup message subscription */
	ubx_modem_msg_unsubscribe(run_state.modem, &pvt_handler_ctx);
#ifdef CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO
	ubx_modem_msg_unsubscribe(run_state.modem, &sat_handler_ctx);
#endif /* CONFIG_TASK_RUNNER_GNSS_SATELLITE_INFO */

	/* Release power requirement */
	rc = pm_device_runtime_put(gnss);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Terminate thread */
	LOG_DBG("Terminating");
}
