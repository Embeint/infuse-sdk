/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
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

#include "gnss_common.h"
#include "zephyr/data/navigation.h"

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_LOCATION);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION)

struct gnss_run_state {
	const struct device *dev;
	const struct task_schedule *schedule;
	struct gnss_data gnss_data_latest;
	struct k_poll_signal gnss_data_sig;
	struct gnss_fix_timeout_state timeout_state;
	int64_t gnss_data_timestamp;
	uint64_t next_time_sync;
	uint32_t task_start;
	uint32_t time_acquired;
	uint8_t flags;
};

static void gnss_data_callback(const struct device *dev, const struct gnss_data *data);
static struct gnss_run_state *cb_state;

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(gnss)), gnss_data_callback);

LOG_MODULE_REGISTER(task_gnss, CONFIG_TASK_GNSS_LOG_LEVEL);

static void gnss_data_callback(const struct device *dev, const struct gnss_data *data)
{
	struct gnss_run_state *state = cb_state;

	if (state == NULL) {
		LOG_WRN("Callback while task not running");
		return;
	}

	state->gnss_data_latest = *data;
	state->gnss_data_timestamp = k_uptime_ticks();
	/* Notify task thread new data is available */
	k_poll_signal_raise(&state->gnss_data_sig, 0);
}

static k_ticks_t data_timestamp(struct gnss_run_state *state)
{
	k_ticks_t timepulse;

	/* Timestamp from timepulse pin if available */
	if (gnss_get_latest_timepulse(state->dev, &timepulse) == 0) {
		return timepulse;
	} else {
		return state->gnss_data_timestamp;
	}
}

static uint64_t log_and_publish(struct gnss_run_state *state, const struct gnss_data *data)
{
	const struct navigation_data *nav = &data->nav_data;
	const struct gnss_info *inf = &data->info;
	int32_t lat = nav->latitude / 100;
	int32_t lon = nav->longitude / 100;
	struct tdf_gcs_wgs84_llha llha = {
		.location =
			{
				.latitude = lat,
				.longitude = lon,
				.height = nav->altitude + inf->geoid_separation,
			},
	};
	uint64_t epoch_time;

	switch (inf->fix_status) {
	case GNSS_FIX_STATUS_ESTIMATED_FIX:
		llha.h_acc = 100000;
		llha.v_acc = 100000;
		break;
	case GNSS_FIX_STATUS_GNSS_FIX:
		llha.h_acc = 1000;
		llha.v_acc = 1000;
		break;
	case GNSS_FIX_STATUS_DGNSS_FIX:
		llha.h_acc = 500;
		llha.v_acc = 500;
		break;
	default:
		/* Set invalid location on insufficient accuracy */
		llha.h_acc = INT32_MAX;
		llha.v_acc = INT32_MAX;
		llha.location.longitude = -1810000000;
		llha.location.latitude = -910000000;
		llha.location.height = 0;
		break;
	}

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, &llha, K_FOREVER);

	/* Timestamp associated with data */
	epoch_time = epoch_time_from_ticks(data_timestamp(state));

	/* Log output */
	TASK_SCHEDULE_TDF_LOG(state->schedule, TASK_GNSS_LOG_LLHA, TDF_GCS_WGS84_LLHA, epoch_time,
			      &llha);

	return epoch_time;
}

uint64_t epoch_time_from_gnss_utc(const struct gnss_time *utc)
{
	struct tm gps_time = {
		.tm_year = 100 + utc->century_year,
		.tm_mon = utc->month - 1,
		.tm_mday = utc->month_day,
		.tm_hour = utc->hour,
		.tm_min = utc->minute,
		.tm_sec = utc->millisecond / 1000,
	};
	time_t unix_time = mktime(&gps_time);
	uint32_t milliseconds = utc->millisecond % 1000;
	uint32_t subseconds = 65536 * milliseconds / 1000;

	return epoch_time_from_unix(unix_time, subseconds);
}

static void gnss_time_update(struct gnss_run_state *state, const char *type)
{
	const struct gnss_time *utc = &state->gnss_data_latest.utc;
	struct timeutil_sync_instant sync = {
		.local = data_timestamp(state),
		.ref = epoch_time_from_gnss_utc(utc),
	};

	LOG_INF("%s time sync from GNSS UTC", type);
	/* Notify time library of sync */
	(void)epoch_time_set_reference(TIME_SOURCE_GNSS, &sync);
}

/* Returns true when task should terminate */
static bool gnss_data_handle(struct gnss_run_state *state, const struct task_gnss_args *args)
{
	const struct navigation_data *nav = &state->gnss_data_latest.nav_data;
	const struct gnss_info *inf = &state->gnss_data_latest.info;
	uint8_t run_target = (args->flags & TASK_GNSS_FLAGS_RUN_MASK);
	int32_t lat = nav->latitude / 100;
	int32_t lon = nav->longitude / 100;
	uint32_t runtime = k_uptime_seconds() - state->task_start;

	/* Periodically print fix state */
	if (k_uptime_seconds() % 30 == 0) {
		LOG_INF("NAV-PVT: Lat: %9d Lon: %9d Height: %6d", lat, lon, nav->altitude);
		LOG_INF("         Status: %d pDOP: %d NumSV: %d", inf->fix_status, inf->hdop / 1000,
			inf->satellites_cnt);
	} else {
		LOG_DBG("NAV-PVT: Lat: %9d Lon: %9d Height: %6d", lat, lon, nav->altitude);
		LOG_DBG("         Status: %d pDOP: %d NumSV: %d", inf->fix_status, inf->hdop / 1000,
			inf->satellites_cnt);
	}

	/* If there is no current time knowledge, or it is old enough, do a quick sync ASAP */
	if ((epoch_time_reference_age() > CONFIG_TASK_RUNNER_GNSS_TIME_COARSE_SYNC_PERIOD_SEC) &&
	    (inf->fix_status == GNSS_FIX_STATUS_ESTIMATED_FIX)) {
		gnss_time_update(state, "Coarse");
	}
	/* Full time knowledge sync */
	if ((k_uptime_get() >= state->next_time_sync) &&
	    ((inf->fix_status == GNSS_FIX_STATUS_GNSS_FIX) ||
	     (inf->fix_status == GNSS_FIX_STATUS_DGNSS_FIX))) {
		gnss_time_update(state, "Fine");
		state->next_time_sync =
			k_uptime_get() +
			(CONFIG_TASK_RUNNER_GNSS_TIME_RESYNC_PERIOD_SEC * MSEC_PER_SEC);
		state->time_acquired = k_uptime_seconds();
	}

	if (run_target == TASK_GNSS_FLAGS_RUN_FOREVER) {
		/* If running perpetually, log each output */
		log_and_publish(state, &state->gnss_data_latest);
	} else if (run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) {
		/* Zephyr GNSS API has no accuracy information (Standard NMEA limitation) */
		if (inf->fix_status == GNSS_FIX_STATUS_GNSS_FIX) {
			return true;
		}
		/* Since there is not accuracy info, we can only check the any fix timeout */
		if ((args->run_to_fix.any_fix_timeout) &&
		    (runtime >= args->run_to_fix.any_fix_timeout) &&
		    (inf->fix_status == GNSS_FIX_STATUS_NO_FIX)) {
			return true;
		}
	} else if (run_target == TASK_GNSS_FLAGS_RUN_TO_TIME_SYNC) {
		if (state->next_time_sync > 0) {
			/* Time has been synchronised */
			return true;
		}
	}
	return false;
}

void gnss_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate,
		  void *gnss_dev)
{
	const struct device *gnss = gnss_dev;
	const struct task_gnss_args *args = &schedule->task_args.infuse.gnss;
	uint8_t run_target = (args->flags & TASK_GNSS_FLAGS_RUN_MASK);
	struct gnss_run_state run_state = {0};
	gnss_systems_t constellations;
	int rc;

	/* Validate the GNSS device */
	if (gnss != DEVICE_DT_GET(DT_ALIAS(gnss))) {
		LOG_DBG("Native Zephyr implementation only supports the 'gnss' alias");
		k_sleep(K_SECONDS(1));
		goto end;
	}

	k_poll_signal_init(&run_state.gnss_data_sig);

	run_state.dev = gnss;
	run_state.schedule = schedule;
	run_state.task_start = k_uptime_seconds();
	cb_state = &run_state;
	gnss_timeout_reset(&run_state.timeout_state);

	LOG_DBG("Starting");

	/* Request sensor to be powered */
	rc = pm_device_runtime_get(gnss);
	if (rc < 0) {
		k_sleep(K_SECONDS(1));
		LOG_ERR("Terminating due to %s", "PM failure");
		goto end;
	}

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

	/* Configure output fix rate */
	rc = gnss_set_fix_rate(gnss, 1000);
	if (rc != 0) {
		LOG_WRN("Failed to configure fix rate (%d)", rc);
	}

	/* Block until runner requests termination (all work happens in NAV-PVT callback) */
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, terminate),
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 &run_state.gnss_data_sig),
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
		k_poll_signal_check(&run_state.gnss_data_sig, &signaled, &result);
		if (signaled) {
			k_poll_signal_reset(&run_state.gnss_data_sig);
			if (gnss_data_handle(&run_state, args)) {
				break;
			}
		}
	}

	/* Log at end of run for a location fix */
	if (run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) {
		const struct gnss_data *fix = &run_state.gnss_data_latest;
		struct tdf_gnss_fix_info fix_info = {
			.time_fix = run_state.time_acquired
					    ? (run_state.time_acquired - run_state.task_start)
					    : UINT16_MAX,
			.location_fix = k_uptime_seconds() - run_state.task_start,
			.num_sv = run_state.gnss_data_latest.info.satellites_cnt,
		};
		int32_t lat = fix->nav_data.latitude / 100;
		int32_t lon = fix->nav_data.longitude / 100;
		int32_t height = (fix->nav_data.altitude + fix->info.geoid_separation) / 1000;
		uint64_t epoch_time;

		LOG_INF("Final Location: Lat %9d Lon %9d Height %dm Status %d", lat, lon, height,
			fix->info.fix_status);
		epoch_time = log_and_publish(&run_state, fix);

		/* Log fix information */
		TASK_SCHEDULE_TDF_LOG(schedule, TASK_GNSS_LOG_FIX_INFO, TDF_GNSS_FIX_INFO,
				      epoch_time, &fix_info);
	}

	/* Release power requirement */
	rc = pm_device_runtime_put(gnss);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Terminate thread */
	LOG_DBG("Terminating");
end:
	cb_state = NULL;
}
