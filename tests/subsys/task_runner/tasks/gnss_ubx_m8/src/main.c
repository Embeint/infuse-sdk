/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gnss.h>

#include <infuse/time/epoch.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/gnss/ubx_emul.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/gnss.h>

#define DEV DEVICE_DT_GET_ONE(u_blox_m8_emul)
GNSS_TASK(1, 0, DEV);
struct task_config config = GNSS_TASK(0, 1, DEV);
struct task_data data;
struct task_schedule schedule = {.task_id = TASK_ID_GNSS};
struct task_schedule_state state;
const gnss_systems_t default_constellations =
	GNSS_SYSTEM_GPS | GNSS_SYSTEM_QZSS | GNSS_SYSTEM_SBAS | GNSS_SYSTEM_GLONASS;

static K_SEM_DEFINE(location_published, 0, 1);

void location_new_data_cb(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_LOCATION);
ZBUS_LISTENER_DEFINE(location_listener, location_new_data_cb);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_LOCATION), location_listener, 5);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION)

#define M  1000
#define KM (1000 * M)

void location_new_data_cb(const struct zbus_channel *chan)
{
	k_sem_give(&location_published);
}

static k_tid_t task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);

	return k_thread_create(&data->executor.thread, config.executor.thread.stack,
			       config.executor.thread.stack_size,
			       (k_thread_entry_t)config.executor.thread.task_fn, (void *)&schedule,
			       &data->terminate_signal, config.task_arg.arg, 5, 0, K_NO_WAIT);
}

static void task_terminate(struct task_data *data)
{
	k_poll_signal_raise(&data->terminate_signal, 0);
}

static void run_location_fix(k_tid_t thread, int32_t latitude, int32_t longitude, int32_t height,
			     uint32_t plateau_start, uint32_t plateau_slope, uint32_t plateau_end,
			     uint32_t final_accuracy, uint8_t final_num_sv)
{
	uint32_t accuracy;

	/* Wait 20 seconds to simulate cold boot */
	if (k_thread_join(thread, K_SECONDS(20)) == 0) {
		return;
	}

	/* Initially has some time knowledge */
	ubx_gnss_nav_pvt_configure(DEV, 0, 0, UINT32_MAX, UINT32_MAX, UINT32_MAX,
				   100 * NSEC_PER_MSEC, UINT16_MAX, 0);
	if (k_thread_join(thread, K_SECONDS(1)) == 0) {
		return;
	}
	ubx_gnss_nav_pvt_configure(DEV, 0, 0, UINT32_MAX, UINT32_MAX, UINT32_MAX, 1 * NSEC_PER_MSEC,
				   UINT16_MAX, 0);
	if (k_thread_join(thread, K_SECONDS(4)) == 0) {
		return;
	}
	/* Poor initial fix, 100ms time accuracy */
	ubx_gnss_nav_pvt_configure(DEV, latitude, longitude, height, 15 * KM, 500 * M,
				   100 * NSEC_PER_MSEC, 1000, 3);
	if (k_thread_join(thread, K_SECONDS(5)) == 0) {
		return;
	}
	/* Quickly improve from 200m to plateau value */
	accuracy = 100 * M;
	while (accuracy >= plateau_start) {
		ubx_gnss_nav_pvt_configure(DEV, latitude, longitude, height, accuracy, 100 * M,
					   10 * NSEC_PER_MSEC, 500, 3);
		if (k_thread_join(thread, K_SECONDS(1)) == 0) {
			return;
		}
		accuracy -= 20 * M;
	}

	/* Plateau the improvmement, 50ns time accuracy */
	accuracy = plateau_start;
	while (accuracy > plateau_end) {
		ubx_gnss_nav_pvt_configure(DEV, latitude, longitude, height, accuracy, 50 * M, 50,
					   150, 8);
		if (k_thread_join(thread, K_SECONDS(1)) == 0) {
			return;
		}
		accuracy -= plateau_slope;
	}

	/* Improve the accuracy until we hit final accuracy */
	while (accuracy > final_accuracy) {
		accuracy -= 2 * M;
		ubx_gnss_nav_pvt_configure(DEV, latitude, longitude, height, accuracy, 10 * M, 50,
					   50, final_num_sv);
		if (k_thread_join(thread, K_SECONDS(1)) == 0) {
			return;
		}
	}
}

static void expected_location_fix(k_tid_t thread, uint32_t start, uint32_t duration)
{
	uint32_t end = k_ticks_to_sec_near32(zbus_chan_publish_time(ZBUS_CHAN));

	/* Final location should be pushed */
	zassert_equal(0, k_sem_take(&location_published, K_SECONDS(2)));
	/* Thread should have terminated */
	zassert_equal(0, k_thread_join(thread, K_NO_WAIT));

	/* Expected duration of the fix */
	zassert_within(duration, end - start, 2);
}

static void expected_no_logging(void)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	zassert_is_null(net_buf_get(tx_queue, K_MSEC(10)));
}

static void expected_logging(int32_t latitude, int32_t longitude, int32_t height, uint32_t h_acc,
			     uint32_t v_acc)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_gcs_wgs84_llha *gcs;
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = net_buf_get(tx_queue, K_MSEC(10));
	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));

	zassert_equal(0, tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_GCS_WGS84_LLHA, &tdf));
	gcs = tdf.data;

	zassert_equal(latitude, gcs->location.latitude);
	zassert_equal(longitude, gcs->location.longitude);
	zassert_equal(height, gcs->location.height);
	zassert_equal(h_acc, gcs->h_acc);
	zassert_equal(v_acc, gcs->v_acc);

	net_buf_unref(pkt);
}

ZTEST(task_gnss_ubx, test_time_fix)
{
	k_tid_t thread;
	uint32_t start;

	schedule.timeout_s = 0;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule.task_logging[0].tdf_mask = 0;
	schedule.task_args.infuse.gnss = (struct task_gnss_args){
		.flags = TASK_GNSS_FLAGS_RUN_TO_TIME_SYNC,
	};

	/* Time should now be valid */
	zassert_equal(TIME_SOURCE_NONE, epoch_time_get_source());

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Run the location fix with a quick plateau */
	run_location_fix(thread, -270000000, 1530000000, 70 * M, 100 * M, 5 * M, 20 * M, 5 * M, 16);

	/* No location should be published */
	zassert_equal(-EAGAIN, k_sem_take(&location_published, K_SECONDS(2)));
	/* Thread should have terminated */
	zassert_equal(0, k_thread_join(thread, K_NO_WAIT));
	/* Time should now be valid */
	zassert_equal(TIME_SOURCE_GNSS, epoch_time_get_source());
}

ZTEST(task_gnss_ubx, test_location_fix)
{
	gnss_systems_t enabled;
	k_tid_t thread;
	uint32_t start;

	schedule.timeout_s = 0;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule.task_logging[0].tdf_mask = 0;
	schedule.task_args.infuse.gnss = (struct task_gnss_args){
		.flags = TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX,
		.accuracy_m = 5,
		.position_dop = 100,
	};

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Run the location fix with a quick plateau */
	run_location_fix(thread, -270000000, 1530000000, 70 * M, 100 * M, 5 * M, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 55);
	expected_no_logging();

	/* Expect default constellations */
	gnss_get_enabled_systems(DEV, &enabled);
	zassert_equal(default_constellations, enabled);
}

ZTEST(task_gnss_ubx, test_location_fix_constellations)
{
	gnss_systems_t enabled;
	k_tid_t thread;
	uint32_t start;

	schedule.timeout_s = 0;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule.task_logging[0].tdf_mask = 0;
	schedule.task_args.infuse.gnss = (struct task_gnss_args){
		.constellations = GNSS_SYSTEM_GPS | GNSS_SYSTEM_QZSS,
		.flags = TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX,
		.accuracy_m = 5,
		.position_dop = 100,
	};

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Run the location fix with a quick plateau */
	run_location_fix(thread, -270000000, 1530000000, 70 * M, 100 * M, 5 * M, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 55);
	expected_no_logging();

	/* Expect requested constellations */
	gnss_get_enabled_systems(DEV, &enabled);
	zassert_equal(GNSS_SYSTEM_GPS | GNSS_SYSTEM_QZSS, enabled);
}

static void task_terminator(struct k_work *work)
{
	task_terminate(&data);
}

ZTEST(task_gnss_ubx, test_location_fix_runner_terminate)
{
	struct k_work_delayable terminator;
	k_tid_t thread;
	uint32_t start;

	schedule.timeout_s = 0;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule.task_logging[0].tdf_mask = TASK_GNSS_LOG_LLHA;
	schedule.task_args.infuse.gnss = (struct task_gnss_args){
		.flags = TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX,
		.accuracy_m = 5,
		.position_dop = 100,
	};

	k_work_init_delayable(&terminator, task_terminator);

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Run the location fix that will be terminated early */
	k_work_reschedule(&terminator, K_MSEC(44500));
	run_location_fix(thread, -270000000, 1530000000, 70 * M, 100 * M, 5 * M, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 46);
	expected_logging(-270000000, 1530000000, 70 * M, 40 * M, 50 * M);

	/* Time should be valid despite the early exit */
	zassert_equal(TIME_SOURCE_GNSS, epoch_time_get_source());
}

ZTEST(task_gnss_ubx, test_location_fix_plateau)
{
	k_tid_t thread;
	uint32_t start;

	schedule.timeout_s = 0;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule.task_logging[0].tdf_mask = TASK_GNSS_LOG_LLHA;
	schedule.task_args.infuse.gnss = (struct task_gnss_args){
		.flags = TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX,
		.accuracy_m = 5,
		.position_dop = 100,
		.run_to_fix =
			{
				.any_fix_timeout = SEC_PER_MIN,
				.fix_plateau =
					{
						.min_accuracy_improvement = 1,
						.timeout = 5,
					},
			},
	};

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Run the location fix with a quick plateau that does not trigger */
	run_location_fix(thread, 550000000, -270000000, 70 * M, 100 * M, 5 * M, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 55);
	expected_logging(550000000, -270000000, 70 * M, 4 * M, 10 * M);

	/* Time should be valid */
	zassert_equal(TIME_SOURCE_GNSS, epoch_time_get_source());
}

ZTEST(task_gnss_ubx, test_location_fix_plateau_timeout)
{
	k_tid_t thread;
	uint32_t start;

	schedule.timeout_s = 0;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule.task_logging[0].tdf_mask = TASK_GNSS_LOG_LLHA;
	schedule.task_args.infuse.gnss = (struct task_gnss_args){
		.flags = TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX,
		.accuracy_m = 5,
		.position_dop = 100,
		.run_to_fix =
			{
				.any_fix_timeout = SEC_PER_MIN,
				.fix_plateau =
					{
						.min_accuracy_improvement = 1,
						.timeout = 5,
					},
			},
	};

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Run the location fix with a slow plateau that should trigger timeout */
	run_location_fix(thread, 230000000, -1500000000, 70 * M, 25 * M, 10, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 41);
	expected_logging(230000000, -1500000000, 70 * M, 24950, 50 * M);

	/* Time should be valid */
	zassert_equal(TIME_SOURCE_GNSS, epoch_time_get_source());
}

ZTEST(task_gnss_ubx, test_location_fix_no_location_timeout)
{
	k_tid_t thread;
	uint32_t start;

	schedule.timeout_s = 0;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule.task_logging[0].tdf_mask = TASK_GNSS_LOG_LLHA;
	schedule.task_args.infuse.gnss = (struct task_gnss_args){
		.flags = TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX,
		.accuracy_m = 5,
		.position_dop = 100,
		.run_to_fix =
			{
				.any_fix_timeout = 15,
				.fix_plateau =
					{
						.min_accuracy_improvement = 1,
						.timeout = 5,
					},
			},
	};

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Should time out before any location is known */
	run_location_fix(thread, 230000000, -1500000000, 70 * M, 25 * M, 10, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 15);
	expected_logging(-910000000, -1810000000, 0, INT32_MAX, INT32_MAX);

	/* No time source as a result of this run */
	zassert_equal(TIME_SOURCE_NONE, epoch_time_get_source());
}

static void logger_before(void *fixture)
{
	epoch_time_reset();
	k_sem_reset(&location_published);
	gnss_set_enabled_systems(DEV, default_constellations);
	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);
}

ZTEST_SUITE(task_gnss_ubx, NULL, NULL, logger_before, NULL, NULL);
