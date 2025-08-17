/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/algorithm_runner/algorithms/tilt.h>
#include <infuse/math/common.h>
#include <infuse/states.h>
#include <infuse/task_runner/tasks/imu.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_TILT);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_TILT)

LOG_MODULE_REGISTER(alg_tilt, CONFIG_ALG_TILT_LOG_LEVEL);

void algorithm_tilt_fn(const struct zbus_channel *chan, const void *config, void *data)
{
	const struct algorithm_tilt_config *c = config;
	struct algorithm_tilt_data *d = data;
	const struct imu_sample_array *samples;
	struct infuse_zbus_chan_tilt chan_data;
	const struct imu_sample *sample;

	if (chan == NULL) {
		/* Tilt angle starts at 0 (cos(0) == 1.0) */
		iir_filter_single_pole_f32_init(&d->filter, c->iir_filter_alpha, 1.0f);
		d->reference_valid = false;
		return;
	}

	samples = chan->message;

	/* Handle changing reference vectors */
	if (kv_store_reflect_crc() != d->kv_store_crc) {
		LOG_DBG("Refreshing gravity reference");
		/* KV store CRC has changed, maybe the gravity reference has */
		d->reference_valid =
			KV_STORE_READ(KV_KEY_GRAVITY_REFERENCE, &d->gravity) == sizeof(d->gravity);
		d->gravity_mag =
			math_vector_xyz_magnitude(d->gravity.x, d->gravity.y, d->gravity.z);
		d->kv_store_crc = kv_store_reflect_crc();
	}

	/* No reference, nothing to compute a reference against */
	if (!d->reference_valid) {
		LOG_DBG("No reference vector");
		goto early_exit;
	}

	int16_t one_g = imu_accelerometer_1g(samples->accelerometer.full_scale_range);
	int16_t one_g_percent = (c->one_g_percent * (int32_t)one_g) / 100;
	int16_t limit_lower = one_g - one_g_percent;
	int16_t limit_upper = one_g + one_g_percent;
	uint32_t limit_lower_sq = limit_lower * limit_lower;
	uint32_t limit_upper_sq = limit_upper * limit_upper;
	k_ticks_t process_start, process_end, last_acc;
	uint32_t sample_mag_sq, divisor;
	float dot_product, cosine;

	/* Iterate over accelerometer samples */
	sample = &samples->samples[samples->accelerometer.offset];
	process_start = k_uptime_ticks();
	for (int i = 0; i < samples->accelerometer.num; i++) {
		sample_mag_sq = math_vector_xyz_sq_magnitude(sample->x, sample->y, sample->z);
		if ((sample_mag_sq < limit_lower_sq) || (sample_mag_sq > limit_upper_sq)) {
			/* Device is not stationary, cannot determine tilt from accelerometer */
			LOG_DBG("Cannot determine tilt");
			goto early_exit;
		}

		/* Compute cos(theta) using the identity:
		 *     cos(theta) = (a.b) / (|a|*|b|)
		 */
		dot_product = math_vector_xyz_dot_product_fast(
			d->gravity.x, d->gravity.y, d->gravity.z, sample->x, sample->y, sample->z);
		divisor =
			d->gravity_mag * math_vector_xyz_magnitude(sample->x, sample->y, sample->z);
		cosine = dot_product / divisor;

		/* With infinite numerical precision, the cosine is guaranteed to fall in the range
		 * [-1, 1] inclusive. Unfortunately because we are using integers, the square root
		 * operation on the two magnitudes loses the fractional part of the divisor. When
		 * theta is very close to zero, these lost fractions can result in the divisor being
		 * very slightly less than the numerator, resulting a cosine value outside of the
		 * valid range. Converting values to `float` to recover the fraction actually
		 * results in worse errors, since the significand of a 32 bit floating point is only
		 * 23 bits long, so the overall data precision is worse.
		 *
		 * Switching to doubles would fix the issue, but would have severe performance
		 * implications. Instead we just limit the values, which only takes effect at the
		 * extreme tilts (0 and 180 degress)
		 */
		cosine = MIN(cosine, 1.0f);
		cosine = MAX(cosine, -1.0f);

		/* Update the IIR filter */
		iir_filter_single_pole_f32_step(&d->filter, cosine);
		sample++;
	}
	process_end = k_uptime_ticks();
	last_acc = imu_sample_timestamp(&samples->accelerometer, samples->accelerometer.num - 1);
	LOG_DBG("Processed %d samples in %d us", samples->accelerometer.num,
		k_ticks_to_us_near32(process_end - process_start));

	/* Finished with zbus channel, release before logging */
	zbus_chan_finish(chan);

	/* Publish the latest angle */
	chan_data.cosine = d->filter.y_prev;
	zbus_chan_pub(ZBUS_CHAN, &chan_data, K_FOREVER);

	/* Log output TDF */
	algorithm_runner_tdf_log(&c->common, ALGORITHM_TILT_LOG_ANGLE, TDF_DEVICE_TILT,
				 sizeof(chan_data), epoch_time_from_ticks(last_acc),
				 &chan_data.cosine);
	return;
early_exit:
	zbus_chan_finish(chan);
	return;
}
