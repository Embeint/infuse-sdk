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

#include <infuse/math/filter.h>

ZTEST(filters, test_iir_filter_single_pole_s16)
{
	struct iir_filter_single_pole_s16 f;
	int32_t out;

	zassert_equal(UINT32_MAX / 2 + 1, iir_filter_alpha_init(0.5f));

	/* Unit decay (1 - e^-1 ~= 0.63212), ~36.7% of original value after 1 step */
	iir_filter_single_pole_s16_init(&f, iir_filter_alpha_init(0.63212f), 10000);
	out = iir_filter_single_pole_s16_step(&f, 0);
	zassert_within(3678, out, 1);

	/* Half unit decay (1 - e^-0.5 ~= 0.39347), ~36.7% of original value after 2 steps */
	iir_filter_single_pole_s16_init(&f, iir_filter_alpha_init(0.39347f), 10000);
	out = iir_filter_single_pole_s16_step(&f, 0);
	zassert_within(6065, out, 1);
	out = iir_filter_single_pole_s16_step(&f, 0);
	zassert_within(3678, out, 1);

	/* After many steps decays to 0 */
	for (int i = 0; i < 100; i++) {
		out = iir_filter_single_pole_s16_step(&f, 0);
	}
	zassert_equal(0, out);

	/* Unit decay (1 - e^-1 ~= 0.63212), step response */
	iir_filter_single_pole_s16_init(&f, iir_filter_alpha_init(0.63212f), 0);
	out = iir_filter_single_pole_s16_step(&f, 10000);
	zassert_within(6321, out, 1);
	out = iir_filter_single_pole_s16_step(&f, 10000);
	zassert_within(8647, out, 1);

	/* After many steps equals the input */
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s16_step(&f, 10000);
	}
	zassert_equal(10000, out);

	/* Unit decay (1 - e^-1 ~= 0.63212), step response negative */
	iir_filter_single_pole_s16_init(&f, iir_filter_alpha_init(0.63212f), 0);
	out = iir_filter_single_pole_s16_step(&f, -10000);
	zassert_within(-6321, out, 1);
	out = iir_filter_single_pole_s16_step(&f, -10000);
	zassert_within(-8647, out, 1);

	/* After many steps equals the input */
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s16_step(&f, -10000);
	}
	zassert_equal(-10000, out);

	/* Another step increase */
	out = iir_filter_single_pole_s16_step(&f, -20000);
	zassert_within(-16321, out, 1);
	out = iir_filter_single_pole_s16_step(&f, -20000);
	zassert_within(-18647, out, 1);
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s16_step(&f, -20000);
	}
	zassert_equal(-20000, out);

	/* Unit decay (1 - e^-1 ~= 0.63212), maximum and minimum limits */
	iir_filter_single_pole_s16_init(&f, iir_filter_alpha_init(0.63212f), 0);
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s16_step(&f, INT16_MAX);
	}
	zassert_equal(INT16_MAX, out);
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s16_step(&f, INT16_MIN);
	}
	zassert_equal(INT16_MIN, out);
}

ZTEST(filters, test_iir_filter_single_pole_s32)
{
	struct iir_filter_single_pole_s32 f;
	int32_t out;

	zassert_equal(UINT32_MAX / 2 + 1, iir_filter_alpha_init(0.5f));

	/* Unit decay (1 - e^-1 ~= 0.63212), ~36.7% of original value after 1 step */
	iir_filter_single_pole_s32_init(&f, iir_filter_alpha_init(0.63212f), 10000);
	out = iir_filter_single_pole_s32_step(&f, 0);
	zassert_within(3678, out, 1);

	/* Half unit decay (1 - e^-0.5 ~= 0.39347), ~36.7% of original value after 2 steps */
	iir_filter_single_pole_s32_init(&f, iir_filter_alpha_init(0.39347f), 10000);
	out = iir_filter_single_pole_s32_step(&f, 0);
	zassert_within(6065, out, 1);
	out = iir_filter_single_pole_s32_step(&f, 0);
	zassert_within(3678, out, 1);

	/* After many steps decays to 0 */
	for (int i = 0; i < 100; i++) {
		out = iir_filter_single_pole_s32_step(&f, 0);
	}
	zassert_equal(0, out);

	/* Unit decay (1 - e^-1 ~= 0.63212), step response */
	iir_filter_single_pole_s32_init(&f, iir_filter_alpha_init(0.63212f), 0);
	out = iir_filter_single_pole_s32_step(&f, 10000);
	zassert_within(6321, out, 1);
	out = iir_filter_single_pole_s32_step(&f, 10000);
	zassert_within(8647, out, 1);

	/* After many steps equals the input */
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s32_step(&f, 10000);
	}
	zassert_equal(10000, out);

	/* Unit decay (1 - e^-1 ~= 0.63212), step response negative */
	iir_filter_single_pole_s32_init(&f, iir_filter_alpha_init(0.63212f), 0);
	out = iir_filter_single_pole_s32_step(&f, -10000);
	zassert_within(-6321, out, 1);
	out = iir_filter_single_pole_s32_step(&f, -10000);
	zassert_within(-8647, out, 1);

	/* After many steps equals the input */
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s32_step(&f, -10000);
	}
	zassert_equal(-10000, out);

	/* Another step increase */
	out = iir_filter_single_pole_s32_step(&f, -20000);
	zassert_within(-16321, out, 1);
	out = iir_filter_single_pole_s32_step(&f, -20000);
	zassert_within(-18647, out, 1);
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s32_step(&f, -20000);
	}
	zassert_equal(-20000, out);

	/* Unit decay (1 - e^-1 ~= 0.63212), maximum and minimum limits */
	iir_filter_single_pole_s32_init(&f, iir_filter_alpha_init(0.63212f), 0);
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s32_step(&f, INT32_MAX);
	}
	zassert_equal(INT32_MAX, out);
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_s32_step(&f, INT32_MIN);
	}
	zassert_equal(INT32_MIN, out);
}

ZTEST(filters, test_iir_filter_single_pole_f32)
{
	struct iir_filter_single_pole_f32 f;
	float out;

	/* Unit decay (1 - e^-1 ~= 0.63212), ~36.7% of original value after 1 step */
	iir_filter_single_pole_f32_init(&f, 0.63212f, 10000.0f);
	out = iir_filter_single_pole_f32_step(&f, 0);
	zassert_within(3678.0f, out, 1.0f);

	/* Half unit decay (1 - e^-0.5 ~= 0.39347), ~36.7% of original value after 2 steps */
	iir_filter_single_pole_f32_init(&f, 0.39347f, 10000.0f);
	out = iir_filter_single_pole_f32_step(&f, 0.0f);
	zassert_within(6065.0f, out, 1.0f);
	out = iir_filter_single_pole_f32_step(&f, 0.0f);
	zassert_within(3678.0f, out, 1.0f);

	/* After many steps decays to 0 */
	for (int i = 0; i < 100; i++) {
		out = iir_filter_single_pole_f32_step(&f, 0);
	}
	zassert_within(0.0f, out, 0.1f);

	/* Unit decay (1 - e^-1 ~= 0.63212), step response */
	iir_filter_single_pole_f32_init(&f, 0.63212f, 0.0f);
	out = iir_filter_single_pole_f32_step(&f, 10000.0f);
	zassert_within(6321.0f, out, 1.0f);
	out = iir_filter_single_pole_f32_step(&f, 10000.0f);
	zassert_within(8647.0f, out, 1.0f);

	/* After many steps very close to the input */
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_f32_step(&f, 10000.0f);
	}
	zassert_within(10000.0f, out, 0.001f);

	/* Unit decay (1 - e^-1 ~= 0.63212), step response negative */
	iir_filter_single_pole_f32_init(&f, 0.63212f, 0.0f);
	out = iir_filter_single_pole_f32_step(&f, -10000.0f);
	zassert_within(-6321.0f, out, 1.0f);
	out = iir_filter_single_pole_f32_step(&f, -10000.0f);
	zassert_within(-8647.0f, out, 1.0f);

	/* After many steps very close to the input */
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_f32_step(&f, -10000.0f);
	}
	zassert_within(-10000, out, 0.001f);

	/* Another step increase */
	out = iir_filter_single_pole_f32_step(&f, -20000.0f);
	zassert_within(-16321.0f, out, 1.0f);
	out = iir_filter_single_pole_f32_step(&f, -20000.0f);
	zassert_within(-18647.0f, out, 1.0f);
	for (int i = 0; i < 25; i++) {
		out = iir_filter_single_pole_f32_step(&f, -20000.0f);
	}
	zassert_within(-20000.0f, out, 0.001f);
}

ZTEST_SUITE(filters, NULL, NULL, NULL, NULL, NULL);
