/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/gnss.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

#include <nrf_modem_gnss.h>
#include <modem/lte_lc.h>

#include "gnss_common.h"

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_LOCATION);
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_NRF9X_NAV_PVT);
#define ZBUS_CHAN     INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION)
#define ZBUS_CHAN_PVT INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_NRF9X_NAV_PVT)

enum {
	TIME_SYNC_DONE = BIT(0),
};

static struct gnss_run_state {
	const struct task_schedule *schedule;
	struct task_data *running_task;
	struct gnss_fix_timeout_state timeout_state;
	struct tdf_nrf9x_gnss_pvt best_fix;
	k_ticks_t interrupt_time;
	uint64_t next_time_sync;
	uint32_t time_acquired;
	uint32_t task_start;
	atomic_t events;
	uint8_t flags;
} state;

LOG_MODULE_REGISTER(task_gnss, LOG_LEVEL_DBG);

static void gnss_event_handler(int event)
{
	LOG_DBG("GNSS event: %d", event);
	if (event == NRF_MODEM_GNSS_EVT_PVT) {
		state.interrupt_time = k_uptime_ticks();
	}
	if (event == NRF_MODEM_GNSS_EVT_FIX) {
		/* Fix event just duplicates the PVT event */
		return;
	}
	atomic_set_bit(&state.events, event);
	task_workqueue_reschedule(state.running_task, K_NO_WAIT);
}

static int nrf9x_gnss_boot(const struct task_gnss_args *args)
{
	int rc;

	rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);
	if (rc != 0) {
		LOG_ERR("Failed to %s (%d)", "activate GNSS", rc);
		return rc;
	}
	rc = nrf_modem_gnss_event_handler_set(gnss_event_handler);
	if (rc != 0) {
		LOG_ERR("Failed to %s (%d)", "set event handler", rc);
		return rc;
	}
	rc = nrf_modem_gnss_use_case_set(NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START);
	if (rc != 0) {
		LOG_ERR("Failed to %s (%d)", "set use case", rc);
		return rc;
	}
	rc = nrf_modem_gnss_fix_interval_set(1);
	if (rc != 0) {
		LOG_ERR("Failed to %s (%d)", "set fix interval", rc);
		return rc;
	}
	rc = nrf_modem_gnss_start();
	if (rc != 0) {
		LOG_ERR("Failed to %s (%d)", "start GNSS", rc);
		return rc;
	}
	return 0;
}

static void nrf9x_gnss_shutdown(void)
{
	int rc;

	rc = nrf_modem_gnss_stop();
	if (rc != 0) {
		LOG_ERR("Failed to %s (%d)", "stop GNSS", rc);
	}
	rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_DEACTIVATE_GNSS);
	if (rc != 0) {
		LOG_ERR("Failed to %s (%d)", "deactivate GNSS", rc);
	}
}

static uint64_t log_and_publish(struct gnss_run_state *state, const struct tdf_nrf9x_gnss_pvt *pvt)
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
	uint64_t epoch_time;

	/* Set known values on invalid accuracies */
	if ((pvt->h_acc >= INT32_MAX) || (pvt->h_acc == 0)) {
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
	zbus_chan_pub(ZBUS_CHAN_PVT, pvt, K_FOREVER);

	/* Timestamp based on interrupt */
	epoch_time = epoch_time_from_ticks(state->interrupt_time);

	/* Log output */
	TASK_SCHEDULE_TDF_LOG(state->schedule, TASK_GNSS_LOG_LLHA, TDF_GCS_WGS84_LLHA, epoch_time,
			      &llha);
	TASK_SCHEDULE_TDF_LOG(state->schedule, TASK_GNSS_LOG_PVT, TDF_NRF9X_GNSS_PVT, epoch_time,
			      pvt);

	return epoch_time;
}

static int retrieve_and_convert_pvt_frame(struct nrf_modem_gnss_pvt_data_frame *frame,
					  struct tdf_nrf9x_gnss_pvt *tdf)
{
	int rc;

	/* Read the latest data frame */
	rc = nrf_modem_gnss_read(frame, sizeof(*frame), NRF_MODEM_GNSS_DATA_PVT);
	if (rc != 0) {
		LOG_ERR("Failed to %s (%d)", "read PVT frame", rc);
		return rc;
	}

	/* Convert from doubles and floats */
	tdf->lat = frame->latitude * 1e7;
	tdf->lon = frame->longitude * 1e7;
	tdf->height = frame->altitude * 1e3f;
	tdf->h_acc = frame->accuracy * 1e3f;
	tdf->v_acc = frame->altitude_accuracy * 1e3f;
	tdf->h_speed = frame->speed * 1e3f;
	tdf->h_speed_acc = frame->speed_accuracy * 1e3f;
	tdf->v_speed = frame->vertical_speed * 1e3f;
	tdf->v_speed_acc = frame->vertical_speed_accuracy * 1e3f;
	tdf->head_mot = frame->heading * 1e5f;
	tdf->head_acc = frame->heading_accuracy * 1e5f;
	tdf->year = frame->datetime.year;
	tdf->month = frame->datetime.month;
	tdf->day = frame->datetime.day;
	tdf->hour = frame->datetime.hour;
	tdf->min = frame->datetime.minute;
	tdf->sec = frame->datetime.seconds;
	tdf->ms = frame->datetime.ms;
	tdf->p_dop = frame->pdop * 1e2f;
	tdf->h_dop = frame->hdop * 1e2f;
	tdf->v_dop = frame->vdop * 1e2f;
	tdf->t_dop = frame->tdop * 1e2f;
	tdf->flags = frame->flags;

	if (tdf->h_acc == 0) {
		/* nRF modem reports h_acc as 0 when it doesn't know where it is */
		tdf->h_acc = UINT32_MAX;
	}

	if (tdf->h_acc > (CONFIG_TASK_RUNNER_GNSS_MINIMUM_ACCURACY_M * 1000)) {
		/* Set invalid location, not 0,0 */
		tdf->lat = -910000000;
		tdf->lon = -1810000000;
	}

	/* Find satellites used in fix */
	tdf->num_sv = 0;
	for (int i = 0; i < ARRAY_SIZE(frame->sv); i++) {
		if (frame->sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_USED_IN_FIX) {
			tdf->num_sv += 1;
		}
	}
	return 0;
}

/* Returns true when task should terminate */
static bool handle_pvt_frame(const struct task_gnss_args *args)
{
	uint8_t run_target = (args->flags & TASK_GNSS_FLAGS_RUN_MASK);
	struct nrf_modem_gnss_pvt_data_frame frame;
	struct tdf_nrf9x_gnss_pvt tdf;
	uint32_t runtime = k_uptime_seconds() - state.task_start;
	bool valid_time, valid_hacc, valid_pdop;
	int rc;

	/* Get data */
	rc = retrieve_and_convert_pvt_frame(&frame, &tdf);
	if (rc < 0) {
		/* Data retrieval failed, terminate */
		return true;
	}

	/* Periodically print fix state */
	if (k_uptime_seconds() % 30 == 0) {
		LOG_INF("NAV-PVT: Lat: %9d Lon: %9d Height: %6d", tdf.lat, tdf.lon, tdf.height);
		LOG_INF("         HAcc: %u mm VAcc: %u mm pDOP: %d NumSV: %d", tdf.h_acc, tdf.v_acc,
			tdf.p_dop / 100, tdf.num_sv);
	} else {
		LOG_DBG("NAV-PVT: Lat: %9d Lon: %9d Height: %6d", tdf.lat, tdf.lon, tdf.height);
		LOG_DBG("         HAcc: %u mm VAcc: %u mm pDOP: %d NumSV: %d", tdf.h_acc, tdf.v_acc,
			tdf.p_dop / 100, tdf.num_sv);
	}

	if (run_target == TASK_GNSS_FLAGS_RUN_FOREVER) {
		/* If running perpetually, log each output */
		log_and_publish(&state, &tdf);
	} else if (run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) {
		/* If running for a fix, update best location fix */
		if (tdf.h_acc <= state.best_fix.h_acc) {
			memcpy(&state.best_fix, &tdf, sizeof(tdf));
		}
		/* Check if the fix has timed out */
		if (gnss_run_to_fix_timeout(args, &state.timeout_state, tdf.h_acc, runtime)) {
			return true;
		}
	}

	valid_time = (tdf.t_dop > 0) && (tdf.t_dop < 1000) && (tdf.num_sv > 0) && (tdf.year > 0);
	if (valid_time && (k_uptime_get() >= state.next_time_sync)) {
		struct tm gps_time = {
			.tm_year = tdf.year - 1900,
			.tm_mon = tdf.month - 1,
			.tm_mday = tdf.day,
			.tm_hour = tdf.hour,
			.tm_min = tdf.min,
			.tm_sec = tdf.sec,
		};
		time_t unix_time = mktime(&gps_time);
		uint32_t subseconds = 32768 * (uint32_t)tdf.ms / 1000;
		uint64_t epoch_time = epoch_time_from_unix(unix_time, subseconds);
		struct timeutil_sync_instant sync = {
			.local = state.interrupt_time,
			.ref = epoch_time,
		};

		LOG_INF("Time sync @ %02d:%02d:%02d.%03d", tdf.hour, tdf.min, tdf.sec, tdf.ms);
		(void)epoch_time_set_reference(TIME_SOURCE_GNSS, &sync);
		state.flags |= TIME_SYNC_DONE;
		state.next_time_sync =
			k_uptime_get() +
			(CONFIG_TASK_RUNNER_GNSS_TIME_RESYNC_PERIOD_SEC * MSEC_PER_SEC);
		state.time_acquired = k_uptime_seconds();
	}

	valid_hacc = (tdf.h_acc <= (1000 * (uint32_t)args->accuracy_m));
	valid_pdop = (tdf.p_dop <= (10 * (uint32_t)args->position_dop));

	if ((run_target == TASK_GNSS_FLAGS_RUN_TO_TIME_SYNC) && (state.flags & TIME_SYNC_DONE)) {
		/* Time sync done, terminate */
		LOG_INF("Terminating due to %s", "time sync complete");
		return true;
	} else if ((run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) &&
		   (state.flags & TIME_SYNC_DONE) && valid_hacc && valid_pdop) {
		/* Location fix done, terminate */
		LOG_INF("Terminating due to %s", "fix obtained");
		return true;
	}

	/* Continue fix */
	return false;
}

void gnss_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_gnss_args *args = &sch->task_args.infuse.gnss;
	uint8_t run_target = (args->flags & TASK_GNSS_FLAGS_RUN_MASK);
	bool terminate = false;
	int rc;

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		/* Early wake by runner to terminate */
		LOG_INF("Terminating due to %s", "runner request");
		terminate = true;
		goto termination;
	}

	if (task->executor.workqueue.reschedule_counter == 0) {
		/* Initialise task */
		memset(&state, 0x00, sizeof(state));
		state.schedule = sch;
		state.running_task = task;
		state.task_start = k_uptime_seconds();
		state.best_fix.h_acc = UINT32_MAX;
		gnss_timeout_reset(&state.timeout_state);
		LOG_DBG("Starting");
		rc = nrf9x_gnss_boot(args);
		if (rc < 0) {
			state.running_task = NULL;
		}
		return;
	}

	if (state.events == 0) {
		LOG_WRN("No GNSS events received");
	} else {
		LOG_DBG("Pending events: %04lx", state.events);
	}
	if (atomic_test_and_clear_bit(&state.events, NRF_MODEM_GNSS_EVT_PVT)) {
		terminate = handle_pvt_frame(args);
	}
	if (atomic_test_and_clear_bit(&state.events, NRF_MODEM_GNSS_EVT_AGNSS_REQ)) {
		LOG_INF("AGNSS request (ignored)");
	}
	if (atomic_test_and_clear_bit(&state.events, NRF_MODEM_GNSS_EVT_BLOCKED)) {
		LOG_INF("LTE blocking GNSS");
	}
	if (atomic_test_and_clear_bit(&state.events, NRF_MODEM_GNSS_EVT_UNBLOCKED)) {
		LOG_INF("LTE no longer blocking GNSS");
	}
	if (state.events) {
		LOG_WRN("Unhandled events: %04lx", state.events);
		atomic_clear(&state.events);
	}

termination:
	if (terminate) {
		/* Shutdown the GNSS */
		nrf9x_gnss_shutdown();
		state.running_task = NULL;

		/* Log at end of run for a location fix */
		if (run_target == TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX) {
			struct tdf_nrf9x_gnss_pvt *best = &state.best_fix;
			struct tdf_gnss_fix_info fix_info = {
				.time_fix = state.time_acquired
						    ? (state.time_acquired - state.task_start)
						    : UINT16_MAX,
				.location_fix = k_uptime_seconds() - state.task_start,
				.num_sv = state.best_fix.num_sv,
			};
			uint64_t epoch_time;

			LOG_INF("Final Location: Lat %9d Lon %9d Height %dm Acc %dcm", best->lat,
				best->lon, best->height / 1000, best->h_acc / 10);
			epoch_time = log_and_publish(&state, &state.best_fix);

			/* Log fix information */
			TASK_SCHEDULE_TDF_LOG(sch, TASK_GNSS_LOG_FIX_INFO, TDF_GNSS_FIX_INFO,
					      epoch_time, &fix_info);
		}

		LOG_DBG("Terminating");
		return;
	}

	/* Expect another callback within 2 seconds */
	task_workqueue_reschedule(task, K_SECONDS(2));
}
