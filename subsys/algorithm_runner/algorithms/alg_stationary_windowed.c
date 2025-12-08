/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/algorithm_runner/algorithms/stationary_windowed.h>
#include <infuse/drivers/imu.h>
#include <infuse/drivers/imu/data_types.h>
#include <infuse/math/common.h>
#include <infuse/states.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_MOVEMENT_STD_DEV);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_MOVEMENT_STD_DEV)

LOG_MODULE_REGISTER(alg_stationary, CONFIG_ALG_STATIONARY_WINDOWED_LOG_LEVEL);

void algorithm_stationary_windowed_fn(const struct zbus_channel *chan,
				      const struct algorithm_runner_common_config *common,
				      const void *args, void *data)
{
	const struct kv_alg_stationary_windowed_args *a = args;
	struct algorithm_stationary_windowed_data *d = data;
	struct infuse_zbus_chan_movement_std_dev chan_data;
	const struct imu_magnitude_array *magnitudes;
	uint32_t uptime = k_uptime_seconds();
	uint16_t sample_rate;
	float variance_f;
	uint32_t variance;
	uint16_t std_dev;
	bool stationary;
	int16_t one_g;

	if (chan == NULL) {
		goto reset;
	}

	/* Process received magnitudes */
	magnitudes = zbus_chan_const_msg(chan);
	one_g = imu_accelerometer_1g(magnitudes->meta.full_scale_range);
	sample_rate = imu_sample_rate(&magnitudes->meta);
	for (uint8_t i = 0; i < magnitudes->meta.num; i++) {
		statistics_update(&d->stats, MIN(magnitudes->magnitudes[i], INT32_MAX));
	}

	/* Finished with zbus channel, release before taking further action */
	zbus_chan_finish(chan);

	/* Still waiting on window to finish */
	if (uptime < d->window_end) {
		return;
	}

	chan_data.expected_samples = a->args.window_seconds * sample_rate;
	chan_data.movement_threshold = a->args.std_dev_threshold_ug;

	/* Raw variance */
	variance_f = statistics_variance(&d->stats);
	variance = min(variance_f, UINT32_MAX);
	/* Raw standard deviation */
	std_dev = math_sqrt32(variance);

	/* Standard deviation is in the same units as the input data,
	 * so we can convert to micro-g's through the usual equation.
	 */
	chan_data.data.std_dev = (1000000 * (uint64_t)std_dev) / one_g;
	chan_data.data.count = d->stats.n;
	stationary = chan_data.data.std_dev <= a->args.std_dev_threshold_ug;

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, &chan_data, K_FOREVER);

	/* Log output TDF */
	algorithm_runner_tdf_log(&a->logging, ALGORITHM_STATIONARY_WINDOWED_LOG_WINDOW_STD_DEV,
				 TDF_ACC_MAGNITUDE_STD_DEV, sizeof(chan_data.data),
				 epoch_time_now(), &chan_data.data);

	/* Validate number of samples (90 - 110% of expected) */
	if (!IN_RANGE(d->stats.n, 9 * chan_data.expected_samples / 10,
		      11 * chan_data.expected_samples / 10)) {
		LOG_WRN("Unexpected sample count, skipping decision");
		goto reset;
	}

	LOG_DBG("Stationary: %s (%u <= %u)", stationary ? "yes" : "no", chan_data.data.std_dev,
		a->args.std_dev_threshold_ug);
	if (stationary) {
		/* Set state until next decision point.
		 * The timeout is included so that even if the IMU stops producing data, the
		 * state will get cleared.
		 */
		infuse_state_clear(INFUSE_STATE_DEVICE_MOVING);
		if (!infuse_state_set_timeout(INFUSE_STATE_DEVICE_STATIONARY,
					      a->args.window_seconds + 10)) {
			/* State was not previously set */
			infuse_state_set_timeout(INFUSE_STATE_DEVICE_STOPPED_MOVING, 1);
			LOG_INF("Now %s (%u <= %u)", "stationary", chan_data.data.std_dev,
				a->args.std_dev_threshold_ug);
		}
	} else {
		if (infuse_state_clear(INFUSE_STATE_DEVICE_STATIONARY)) {
			/* Stationary state was previously set */
			infuse_state_set_timeout(INFUSE_STATE_DEVICE_STARTED_MOVING, 1);
			LOG_INF("Now %s (%u > %u)", "moving", chan_data.data.std_dev,
				a->args.std_dev_threshold_ug);
		}
		infuse_state_set_timeout(INFUSE_STATE_DEVICE_MOVING, a->args.window_seconds + 10);
	}

reset:
	/* Reset for next window */
	d->window_end = uptime + a->args.window_seconds;
	statistics_reset(&d->stats);
}
