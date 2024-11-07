/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/states.h>
#include <infuse/math/common.h>
#include <infuse/math/statistics.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/imu.h>
#include <infuse/task_runner/tasks/alg_stationary_windowed.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

static void new_mag_data(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_IMU_ACC_MAG);
ZBUS_LISTENER_DEFINE_WITH_ENABLE(mag_listener, new_mag_data, false);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU_ACC_MAG), mag_listener, 5);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU_ACC_MAG)

static struct stationary_state {
	struct task_data *task;
	struct k_work_delayable worker;
	struct statistics_state stats;
	uint32_t window_end;
	uint32_t print_end;
} state;

LOG_MODULE_REGISTER(alg_stationary, CONFIG_TASK_ALG_STATIONARY_LOG_LEVEL);

void new_mag_data(const struct zbus_channel *chan)
{
	task_workqueue_reschedule(state.task, K_NO_WAIT);
}

void task_alg_stationary_windowed_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_alg_stationary_windowed_args *args =
		&sch->task_args.infuse.alg_stationary_windowed;
	const struct imu_magnitude_array *magnitudes;
	struct tdf_acc_magnitude_std_dev tdf;
	uint32_t buffer_period, sample_period, sample_rate, expected_samples;
	uint32_t uptime = k_uptime_seconds();
	uint64_t variance;
	uint64_t std_dev;
	uint8_t range, num;
	bool stationary;

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		/* Early wake by runner to terminate */
		zbus_obs_set_enable(&mag_listener, false);
		return;
	}

	/* Random delay when first scheduled */
	if (task->executor.workqueue.reschedule_counter == 0) {
		state.task = task;
		state.window_end = k_uptime_seconds() + args->window_seconds;
		zbus_obs_set_enable(&mag_listener, true);
		task_workqueue_reschedule(task, K_SECONDS(60));
		return;
	}

	/* Process received magnitudes */
	zbus_chan_claim(ZBUS_CHAN, K_FOREVER);
	magnitudes = ZBUS_CHAN->message;
	range = magnitudes->meta.full_scale_range;
	buffer_period = magnitudes->meta.buffer_period_ticks;
	num = magnitudes->meta.num;
	for (uint8_t i = 0; i < num; i++) {
		statistics_update(&state.stats, MIN(magnitudes->magnitudes[i], INT32_MAX));
	}
	zbus_chan_finish(ZBUS_CHAN);

	/* Provide periodic updates on the state */
	if (uptime >= state.print_end) {
		variance = (uint64_t)statistics_variance_rough(&state.stats);
		std_dev = math_sqrt32(variance);
		chan_data.data.std_dev = (1000000 * std_dev) / imu_accelerometer_1g(range);
		LOG_INF("Running std-dev: %d uG", chan_data.data.std_dev);
		state.print_end = uptime + SEC_PER_MIN;
	}

	/* Still waiting on window to finish */
	if (uptime < state.window_end) {
		task_workqueue_reschedule(task, K_SECONDS(60));
		return;
	}

	/* Raw variance */
	variance = (uint64_t)statistics_variance(&state.stats);
	/* Raw standard deviation */
	std_dev = math_sqrt64(variance);

	/* Standard deviation is in the same units as the input data,
	 * so we can convert to micro-g's through the usual equation.
	 */
	tdf.std_dev = (1000000 * std_dev) / imu_accelerometer_1g(range);
	tdf.count = state.stats.n;
	stationary = tdf.std_dev <= args->std_dev_threshold_ug;

	/* Log output TDF */
	task_schedule_tdf_log(sch, TASK_ALG_STATIONARY_WINDOWED_LOG_WINDOW_STD_DEV,
			      TDF_ACC_MAGNITUDE_STD_DEV, sizeof(tdf), epoch_time_now(), &tdf);

	/* Validate number of samples (90 - 110% of expected) */
	sample_period = buffer_period / (num - 1);
	sample_rate = 1000000 / k_ticks_to_us_near32(sample_period);
	expected_samples = args->window_seconds * sample_rate;
	if (!IN_RANGE(state.stats.n, 9 * expected_samples / 10, 11 * expected_samples / 10)) {
		LOG_WRN("Unexpected sample count, skipping decision");
		goto reset;
	}

	LOG_INF("Stationary: %s (%u <= %u)", stationary ? "yes" : "no", tdf.std_dev,
		args->std_dev_threshold_ug);
	if (stationary) {
		/* Set state until next decision point.
		 * The timeout is included so that even if the IMU stops producing data, the
		 * state will get cleared.
		 */
		infuse_state_set_timeout(INFUSE_STATE_DEVICE_STATIONARY, args->window_seconds + 10);
	} else {
		infuse_state_clear(INFUSE_STATE_DEVICE_STATIONARY);
	}

reset:
	/* Reset for next window */
	state.window_end = uptime + args->window_seconds;
	statistics_reset(&state.stats);
	task_workqueue_reschedule(task, K_SECONDS(60));
}
