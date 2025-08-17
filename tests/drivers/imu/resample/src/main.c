/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <infuse/drivers/imu.h>

static struct imu_sample sample_array[48];
float resampled_x[16];
float resampled_y[16];
float resampled_z[16];

ZTEST(imu_resample, test_linear_downsample_scaled_2_to_1)
{
	struct imu_linear_downsample_scaled_state state = {
		.output_x = resampled_x,
		.output_y = resampled_y,
		.output_z = resampled_z,
		.output_size = ARRAY_SIZE(resampled_x),
		.scale = 1000,
		.freq_mult = 1,
		.freq_div = 2,
		.output_size = 4,
	};
	int consumed;

	for (int i = 0; i < ARRAY_SIZE(sample_array); i++) {
		sample_array[i].x = 1000 + (100 * i);
		sample_array[i].y = 1000 - (100 * i);
		sample_array[i].z = -1000 - (100 * i);
	}

	/* Indicies 0,2,4,6 should be written directly to output */
	consumed = imu_linear_downsample_scaled(&state, sample_array, 7);
	zassert_equal(7, consumed);
	zassert_equal(4, state.output_offset);

	for (int i = 0; i < state.output_offset; i++) {
		zassert_within(1.0f + (i * 0.2f), resampled_x[i], 0.001f);
		zassert_within(1.0f - (i * 0.2f), resampled_y[i], 0.001f);
		zassert_within(-1.0f - (i * 0.2f), resampled_z[i], 0.001f);
	}

	/* Indicies 1,3,5 should be written directly to output */
	state.output_offset = 0;
	consumed = imu_linear_downsample_scaled(&state, sample_array + 7, 7);
	zassert_equal(7, consumed);
	zassert_equal(3, state.output_offset);

	for (int i = 0; i < state.output_offset; i++) {
		zassert_within(1.8f + (i * 0.2f), resampled_x[i], 0.001f);
		zassert_within(0.2f - (i * 0.2f), resampled_y[i], 0.001f);
		zassert_within(-1.8f - (i * 0.2f), resampled_z[i], 0.001f);
	}

	/* Indicies 0,2,4,6 should be written directly to output */
	state.output_offset = 0;
	consumed = imu_linear_downsample_scaled(&state, sample_array + 14, 7);
	zassert_equal(7, consumed);
	zassert_equal(4, state.output_offset);

	for (int i = 0; i < state.output_offset; i++) {
		zassert_within(2.4f + (i * 0.2f), resampled_x[i], 0.001f);
		zassert_within(-0.4f - (i * 0.2f), resampled_y[i], 0.001f);
		zassert_within(-2.4f - (i * 0.2f), resampled_z[i], 0.001f);
	}
}

ZTEST(imu_resample, test_linear_downsample_scaled_4_to_3)
{
	struct imu_linear_downsample_scaled_state state = {
		.output_x = resampled_x,
		.output_y = resampled_y,
		.output_z = resampled_z,
		.output_size = ARRAY_SIZE(resampled_x),
		.scale = 1000,
		.freq_mult = 3,
		.freq_div = 4,
		.output_size = 8,
	};
	int consumed;

	for (int i = 0; i < ARRAY_SIZE(sample_array); i++) {
		sample_array[i].x = 1000 + (100 * i);
		sample_array[i].y = 1000 - (100 * i);
		sample_array[i].z = -1000 - (100 * i);
	}

	consumed = imu_linear_downsample_scaled(&state, sample_array, 12);
	zassert_equal(11, consumed);
	zassert_equal(8, state.output_offset);

	for (int i = 0; i < state.output_offset; i++) {
		float shift = 0.1f * 4.0f / 3.0f;

		zassert_within(1.0f + (i * shift), resampled_x[i], 0.001f);
		zassert_within(1.0f - (i * shift), resampled_y[i], 0.001f);
		zassert_within(-1.0f - (i * shift), resampled_z[i], 0.001f);
	}

	state.output_offset = 0;
	consumed = imu_linear_downsample_scaled(&state, sample_array + 11, 4);
	zassert_equal(4, consumed);
	zassert_true(state.output_offset < state.output_size);
	consumed = imu_linear_downsample_scaled(&state, sample_array + 15, 4);
	zassert_equal(4, consumed);
	zassert_true(state.output_offset < state.output_size);
	consumed = imu_linear_downsample_scaled(&state, sample_array + 19, 4);
	zassert_equal(2, consumed);
	zassert_equal(8, state.output_offset);

	for (int i = 0; i < state.output_offset; i++) {
		float shift = 0.1f * 4.0f / 3.0f;

		zassert_within(2.066f + (i * shift), resampled_x[i], 0.001f);
		zassert_within(-0.066f - (i * shift), resampled_y[i], 0.001f);
		zassert_within(-2.066f - (i * shift), resampled_z[i], 0.001f);
	}
}

ZTEST_SUITE(imu_resample, NULL, NULL, NULL, NULL, NULL);
