/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/algorithm_runner/algorithms/movement_threshold.h>
#include <infuse/drivers/imu.h>
#include <infuse/drivers/imu/data_types.h>
#include <infuse/math/common.h>
#include <infuse/states.h>

LOG_MODULE_REGISTER(alg_movement, CONFIG_ALG_MOVEMENT_THRESHOLD_LOG_LEVEL);

void algorithm_movement_threshold_fn(const struct zbus_channel *chan,
				     const struct algorithm_runner_common_config *common,
				     const void *args, void *data)
{
	const struct kv_alg_movement_threshold_args_v2 *a = args;
	struct algorithm_movement_threshold_data *d = data;
	const struct imu_magnitude_array *magnitudes;
	bool moving = false;

	if (chan == NULL) {
		d->full_scale_range = 0;
		return;
	}

	/* Process received magnitudes */
	magnitudes = zbus_chan_const_msg(chan);

	if (magnitudes->meta.full_scale_range != d->full_scale_range) {
		/* Recompute the thresholds in raw units */
		int32_t one_g_raw = imu_accelerometer_1g(magnitudes->meta.full_scale_range);
		int32_t initial_threshold_raw =
			(one_g_raw * (uint64_t)a->args.initial_threshold_ug) / 1000000;
		int32_t continue_threshold_raw =
			(one_g_raw * (uint64_t)a->args.continue_threshold_ug) / 1000000;

		/* Ensure thresholds never wraps */
		d->initial_threshold_low =
			(initial_threshold_raw < one_g_raw) ? one_g_raw - initial_threshold_raw : 0;
		d->initial_threshold_high = one_g_raw + initial_threshold_raw;
		d->continue_threshold_low = (continue_threshold_raw < one_g_raw)
						    ? one_g_raw - continue_threshold_raw
						    : 0;
		d->continue_threshold_high = one_g_raw + continue_threshold_raw;
		d->full_scale_range = magnitudes->meta.full_scale_range;

		LOG_INF("Initial threshold %u range = [%u - %u]", a->args.initial_threshold_ug,
			d->initial_threshold_low, d->initial_threshold_high);
		LOG_INF("Continue threshold %u range = [%u - %u]", a->args.continue_threshold_ug,
			d->continue_threshold_low, d->continue_threshold_high);
	}

	bool already_moving = infuse_state_get(INFUSE_STATE_DEVICE_MOVING);
	const uint32_t threshold_low =
		already_moving ? d->continue_threshold_low : d->initial_threshold_low;
	const uint32_t threshold_high =
		already_moving ? d->continue_threshold_high : d->initial_threshold_high;

	LOG_DBG("%s, thresholds [%u - %u]", already_moving ? "Moving" : "Stationary", threshold_low,
		threshold_high);

	/* Does any magnitude exceed the threshold? */
	for (uint16_t i = 0; i < magnitudes->meta.num; i++) {
		if ((magnitudes->magnitudes[i] < threshold_low) ||
		    (magnitudes->magnitudes[i] > threshold_high)) {
			moving = true;
			break;
		}
	}

	/* Finished with zbus channel, release before taking further action */
	zbus_chan_finish(chan);

	LOG_DBG("Moving: %s", moving ? "yes" : "no");
	if (moving) {
		/* Extend moving timeout */
		if (!infuse_state_set_timeout(INFUSE_STATE_DEVICE_MOVING, a->args.moving_for)) {
			/* Moving just started */
			LOG_INF("Movement detected, initial timeout %d", a->args.moving_for);
			infuse_state_set_timeout(INFUSE_STATE_DEVICE_STARTED_MOVING, 1);
		}
	}
}
