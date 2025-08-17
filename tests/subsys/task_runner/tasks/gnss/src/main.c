/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
#include <infuse/drivers/gnss/gnss_emul.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/gnss.h>

#define DEV DEVICE_DT_GET(DT_ALIAS(gnss))
GNSS_TASK(1, 0, DEV);
struct task_config config = GNSS_TASK(0, 1, DEV);
struct task_data data;
struct task_schedule schedule = {.validity = TASK_VALID_ALWAYS, .task_id = TASK_ID_GNSS};
struct task_schedule_state state;

static K_SEM_DEFINE(location_published, 0, 1);
static K_SEM_DEFINE(nav_pvt_published, 0, 1);

void location_new_data_cb(const struct zbus_channel *chan);
void nav_pvt_new_data_cb(const struct zbus_channel *chan);

#if defined(CONFIG_TASK_RUNNER_TASK_GNSS_UBX)
#define NAV_PVT_CHANNEL INFUSE_ZBUS_CHAN_UBX_NAV_PVT
#define NAV_PVT_NAME    INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_UBX_NAV_PVT)
#elif defined(CONFIG_TASK_RUNNER_TASK_GNSS_NRF9X)
#define NAV_PVT_CHANNEL INFUSE_ZBUS_CHAN_NRF9X_NAV_PVT
#define NAV_PVT_NAME    INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_NRF9X_NAV_PVT)
#else
#error Unknown GNSS task
#endif

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_LOCATION, NAV_PVT_CHANNEL);
ZBUS_LISTENER_DEFINE(location_listener, location_new_data_cb);
ZBUS_LISTENER_DEFINE(nav_pvt_listener, nav_pvt_new_data_cb);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_LOCATION), location_listener, 5);
ZBUS_CHAN_ADD_OBS(NAV_PVT_NAME, nav_pvt_listener, 5);
#define ZBUS_CHAN     INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION)
#define ZBUS_CHAN_PVT INFUSE_ZBUS_CHAN_GET(NAV_PVT_CHANNEL)

#define M  1000
#define KM (1000 * M)

void location_new_data_cb(const struct zbus_channel *chan)
{
	k_sem_give(&location_published);
}

void nav_pvt_new_data_cb(const struct zbus_channel *chan)
{
	k_sem_give(&nav_pvt_published);
}

static k_tid_t task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	k_poll_signal_init(&data->terminate_signal);

	if (config.exec_type == TASK_EXECUTOR_THREAD) {
		return k_thread_create(config.executor.thread.thread, config.executor.thread.stack,
				       config.executor.thread.stack_size,
				       (k_thread_entry_t)config.executor.thread.task_fn,
				       (void *)&schedule, &data->terminate_signal,
				       config.task_arg.arg, 5, 0, K_NO_WAIT);
	} else {
		data->executor.workqueue.reschedule_counter = 0;
		k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
		return NULL;
	}
}

static bool task_wait(k_tid_t thread, k_timeout_t timeout)
{
	if (thread) {
		/* Wait 20 seconds to simulate cold boot */
		return k_thread_join(thread, timeout) == 0;
	}
	k_sleep(timeout);
	return !k_work_delayable_is_pending(&data.executor.workqueue.work);
}

static void task_terminate(struct task_data *data)
{
	k_poll_signal_raise(&data->terminate_signal, 0);
}

static void run_location_fix(k_tid_t thread, int32_t latitude, int32_t longitude, int32_t height,
			     uint32_t plateau_start, uint32_t plateau_slope, uint32_t plateau_end,
			     uint32_t final_accuracy, uint8_t final_num_sv)
{
	struct gnss_pvt_emul_location emul_loc = {0,          0, UINT32_MAX, UINT32_MAX,
						  UINT32_MAX, 0, UINT16_MAX, 0};

	/* Wait 20 seconds to simulate cold boot */
	if (task_wait(thread, K_SECONDS(20))) {
		return;
	}

	/* Initially has some time knowledge */
	emul_loc.t_acc = 100 * NSEC_PER_MSEC;
	emul_gnss_pvt_configure(DEV, &emul_loc);
	if (task_wait(thread, K_SECONDS(1))) {
		return;
	}
	emul_loc.t_acc = 1 * NSEC_PER_MSEC;
	emul_gnss_pvt_configure(DEV, &emul_loc);
	if (task_wait(thread, K_SECONDS(4))) {
		return;
	}
	/* Poor initial fix, 100ms time accuracy */
	emul_loc.latitude = latitude;
	emul_loc.longitude = longitude;
	emul_loc.height = height;
	emul_loc.h_acc = 15 * KM;
	emul_loc.v_acc = 500 * M;
	emul_loc.t_acc = 100 * NSEC_PER_MSEC;
	emul_loc.p_dop = 1000;
	emul_loc.num_sv = 3;
	emul_gnss_pvt_configure(DEV, &emul_loc);
	if (task_wait(thread, K_SECONDS(5))) {
		return;
	}
	/* Quickly improve from 200m to plateau value */
	emul_loc.h_acc = 100 * M;
	emul_loc.t_acc = 10 * NSEC_PER_MSEC;
	emul_loc.p_dop = 500;
	while (emul_loc.h_acc >= plateau_start) {
		emul_gnss_pvt_configure(DEV, &emul_loc);
		if (task_wait(thread, K_SECONDS(1))) {
			return;
		}
		emul_loc.h_acc -= 20 * M;
	}

	/* Plateau the improvmement, 50ns time accuracy */
	emul_loc.h_acc = plateau_start;
	emul_loc.v_acc = 50 * M;
	emul_loc.t_acc = 50;
	emul_loc.p_dop = 150;
	emul_loc.num_sv = 8;
	while (emul_loc.h_acc > plateau_end) {
		emul_gnss_pvt_configure(DEV, &emul_loc);
		if (task_wait(thread, K_SECONDS(1))) {
			return;
		}
		emul_loc.h_acc -= plateau_slope;
	}

	/* Improve the accuracy until we hit final accuracy */
	emul_loc.v_acc = 10 * M;
	emul_loc.p_dop = 50;
	emul_loc.num_sv = final_num_sv;
	while (emul_loc.h_acc > final_accuracy) {
		emul_loc.h_acc -= 2 * M;
		emul_gnss_pvt_configure(DEV, &emul_loc);
		if (task_wait(thread, K_SECONDS(1))) {
			return;
		}
	}
}

static void expected_location_fix(k_tid_t thread, uint32_t start, uint32_t duration)
{
	uint32_t end = k_ticks_to_sec_near32(zbus_chan_pub_stats_last_time(ZBUS_CHAN));

	/* Final location should be pushed */
	zassert_equal(0, k_sem_take(&location_published, K_SECONDS(2)));
	zassert_equal(0, k_sem_take(&nav_pvt_published, K_MSEC(1)));
	if (thread) {
		/* Thread should have terminated */
		zassert_equal(0, k_thread_join(thread, K_NO_WAIT));
	} else {
		/* Work should have terminated */
		zassert_false(k_work_delayable_is_pending(&data.executor.workqueue.work));
	}

	/* Expected duration of the fix */
	zassert_within(duration, end - start, 2);
}

static void expected_no_logging(void)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	zassert_is_null(k_fifo_get(tx_queue, K_MSEC(10)));
}

static void expected_logging(int32_t latitude, int32_t longitude, int32_t height, uint32_t h_acc,
			     uint32_t v_acc, uint32_t h_acc_threshold)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_gcs_wgs84_llha *gcs;
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));

	zassert_equal(0, tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_GCS_WGS84_LLHA, &tdf));
	gcs = tdf.data;

	zassert_equal(latitude, gcs->location.latitude);
	zassert_equal(longitude, gcs->location.longitude);
	zassert_equal(height, gcs->location.height);
	zassert_within(h_acc, gcs->h_acc, h_acc_threshold);
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
	zassert_equal(-EAGAIN, k_sem_take(&nav_pvt_published, K_MSEC(1)));
	if (thread) {
		/* Thread should have terminated */
		zassert_equal(0, k_thread_join(thread, K_NO_WAIT));
	} else {
		/* Work should have terminated */
		zassert_false(k_work_delayable_is_pending(&data.executor.workqueue.work));
	}
	/* Time should now be valid */
	zassert_equal(TIME_SOURCE_GNSS, epoch_time_get_source());
}

ZTEST(task_gnss_ubx, test_location_fix)
{
	gnss_systems_t sys_default, sys_enabled;
	k_tid_t thread;
	uint32_t start;
	int rc;

	schedule.timeout_s = 0;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule.task_logging[0].tdf_mask = 0;
	schedule.task_args.infuse.gnss = (struct task_gnss_args){
		.flags = TASK_GNSS_FLAGS_RUN_TO_LOCATION_FIX,
		.accuracy_m = 5,
		.position_dop = 100,
	};

	rc = gnss_get_enabled_systems(DEV, &sys_default);
	zassert_equal(0, rc);

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Run the location fix with a quick plateau */
	run_location_fix(thread, -270000000, 1530000000, 70 * M, 100 * M, 5 * M, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 55);
	expected_no_logging();

	/* Expect default constellations */
	rc = gnss_get_enabled_systems(DEV, &sys_enabled);
	zassert_equal(0, rc);
	zassert_equal(sys_default, sys_enabled);
}

ZTEST(task_gnss_ubx, test_location_fix_constellations)
{
	gnss_systems_t sys_enabled;
	k_tid_t thread;
	uint32_t start;
	int rc;

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
	rc = gnss_get_enabled_systems(DEV, &sys_enabled);
	zassert_equal(0, rc);
	zassert_equal(GNSS_SYSTEM_GPS | GNSS_SYSTEM_QZSS, sys_enabled);
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
	expected_logging(-270000000, 1530000000, 70 * M, 40 * M, 50 * M, 1 * M);

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
						.min_accuracy_improvement_m = 1,
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
	expected_logging(550000000, -270000000, 70 * M, 4 * M, 10 * M, 1 * M);

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
						.min_accuracy_improvement_m = 1,
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
	expected_logging(230000000, -1500000000, 70 * M, 24950, 50 * M, 1 * M);

	/* Time should be valid */
	zassert_equal(TIME_SOURCE_GNSS, epoch_time_get_source());
}

ZTEST(task_gnss_ubx, test_location_fix_plateau_min_accuracy)
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
		.run_to_fix =
			{
				.any_fix_timeout = SEC_PER_MIN,
				.fix_plateau =
					{
						.min_accuracy_m = 20,
						.min_accuracy_improvement_m = 1,
						.timeout = 5,
					},
			},
	};

	k_work_init_delayable(&terminator, task_terminator);

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	k_work_reschedule(&terminator, K_SECONDS(120));

	/* Run the location fix with a plateau before the minimum accuracy is reached */
	run_location_fix(thread, 230000000, -1500000000, 70 * M, 25 * M, 10, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 120);
	expected_logging(230000000, -1500000000, 70 * M, 24160, 50 * M, 1 * M);

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
						.min_accuracy_improvement_m = 1,
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
	expected_logging(-910000000, -1810000000, 0, INT32_MAX, INT32_MAX, 0);

	/* No time source as a result of this run */
	zassert_equal(TIME_SOURCE_NONE, epoch_time_get_source());
}

ZTEST(task_gnss_ubx, test_pm_failure)
{
#ifdef CONFIG_GNSS_UBX_MODEM_EMUL
	int *pm_rc, *comms_reset_cnt;
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

	/* Next call to PM returns an error, comms reset should be run */
	emul_gnss_ubx_dev_ptrs(DEV, &pm_rc, &comms_reset_cnt);
	zassert_equal(0, *comms_reset_cnt);
	*pm_rc = -EIO;

	/* Schedule a location fix that completes in <1 minute */
	start = k_uptime_seconds();
	thread = task_schedule(&data);

	/* Run the location fix with a quick plateau */
	run_location_fix(thread, -270000000, 1530000000, 70 * M, 100 * M, 5 * M, 20 * M, 5 * M, 16);
	expected_location_fix(thread, start, 55);
	expected_no_logging();

	/* Comms reset should have been called due to PM failure */
	zassert_equal(1, *comms_reset_cnt);
#endif /* CONFIG_GNSS_UBX_MODEM_EMUL */
}

static void logger_before(void *fixture)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *buf;

	epoch_time_reset();
	k_sem_reset(&location_published);
	k_sem_reset(&nav_pvt_published);
	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	while (1) {
		buf = k_fifo_get(tx_queue, K_MSEC(10));
		if (buf == NULL) {
			break;
		}
		net_buf_unref(buf);
	}

#ifdef CONFIG_GNSS_NRF9X_EMUL
	struct gnss_pvt_emul_location emul_loc = {0};
	/* nRF modem reports all fields as 0 on boot */
	emul_gnss_pvt_configure(DEV, &emul_loc);
#endif /* CONFIG_GNSS_NRF9X_EMUL */
#ifdef CONFIG_GNSS_UBX_MODEM_EMUL
	struct gnss_pvt_emul_location emul_loc = {0,          0,          UINT32_MAX, UINT32_MAX,
						  UINT32_MAX, UINT32_MAX, UINT16_MAX, 0};
	/* UBX modem reports all fields as 1 on boot */
	emul_gnss_pvt_configure(DEV, &emul_loc);
#endif /* CONFIG_GNSS_UBX_MODEM_EMUL */
}

ZTEST_SUITE(task_gnss_ubx, NULL, NULL, logger_before, NULL, NULL);
