/*
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <math.h>

#include <zephyr/ztest.h>

#include <infuse/math/statistics.h>

#define ABS(a) (((a) < 0) ? -(a) : (a))

static void run_test_case(const int32_t *values, const float *means, const float *variances,
			  size_t num)
{
	float mean, mean_perc, variance, variance_rough, variance_perc;
	struct statistics_state s;

	statistics_reset(&s);
	for (int i = 0; i < num; i++) {
		statistics_update(&s, values[i]);

		/* Values within 0.1% of expected */
		mean = statistics_mean(&s);
		mean_perc = 1.0f - (mean / means[i]);
		zassert_true(mean_perc < 0.0001f);
		if (i > 0) {
			variance = statistics_variance(&s);
			variance_perc = 1.0f - (variance / variances[i]);
			zassert_true(variance_perc < 0.0001f);

			variance_rough = statistics_variance_rough(&s);
			variance_perc = 1.0f - (variance / variance_rough);
			/* Within 1.0f or 2% */
			if (ABS(variance - variance_rough) > 1.0f) {
				zassert_true(variance_perc < 0.02f);
			}
		}

		/* Within a whole number of the float value */
		zassert_within(mean, (float)statistics_mean_rough(&s), 1.0f);
	}
}

ZTEST(infuse_stats, test_constant)
{
	struct statistics_state s;

	statistics_reset(&s);

	for (int i = 0; i < 100; i++) {
		statistics_update(&s, 0);

		zassert_within(0.0f, statistics_mean(&s), 0.001f);
		zassert_within(0.0f, statistics_variance(&s), 0.001f);
	}

	statistics_reset(&s);

	for (int i = 0; i < 100; i++) {
		statistics_update(&s, 10);

		zassert_within(10.0f, statistics_mean(&s), 0.001f);
		zassert_within(0.0f, statistics_variance(&s), 0.001f);
	}

	statistics_reset(&s);

	for (int i = 0; i < 100; i++) {
		statistics_update(&s, -1000);

		zassert_within(-1000.0f, statistics_mean(&s), 0.001f);
		zassert_within(0.0f, statistics_variance(&s), 0.001f);
	}
}

ZTEST(infuse_stats, test_easy_sequences)
{
	struct statistics_state s;

	statistics_reset(&s);
	statistics_update(&s, 0);
	statistics_update(&s, 5);
	statistics_update(&s, -5);
	zassert_within(0.0f, statistics_mean(&s), 0.001f);
	zassert_within(25.0f, statistics_variance(&s), 0.001f);

	statistics_reset(&s);
	statistics_update(&s, 0);
	statistics_update(&s, 10);
	statistics_update(&s, -10);
	zassert_within(0.0f, statistics_mean(&s), 0.001f);
	zassert_within(100.0f, statistics_variance(&s), 0.001f);
	statistics_update(&s, 0);
	statistics_update(&s, 0);
	zassert_within(0.0f, statistics_mean(&s), 0.001f);
	zassert_within(50.0f, statistics_variance(&s), 0.001f);

	statistics_reset(&s);
	statistics_update(&s, 0);
	statistics_update(&s, 10);
	statistics_update(&s, 20);
	statistics_update(&s, 30);
	statistics_update(&s, 40);
	statistics_update(&s, 50);
	zassert_within(25.0f, statistics_mean(&s), 0.001f);
	zassert_within(350.0f, statistics_variance(&s), 0.001f);
	statistics_update(&s, 25);
	zassert_within(291.666f, statistics_variance(&s), 0.001f);
}

ZTEST(infuse_stats, test_limits)
{
	struct statistics_state s;

	statistics_reset(&s);
	statistics_update(&s, 0);
	statistics_update(&s, INT32_MAX);
	statistics_update(&s, -INT32_MAX);
	zassert_within(0.0f, statistics_mean(&s), 0.001f);
	zassert_within(4611686014132420608, statistics_variance_rough(&s), 2);
}

ZTEST(infuse_stats, test_init)
{
	struct statistics_state s;

	statistics_reset(&s);
	zassert_within(0.0f, statistics_mean(&s), 0.001f);
	zassert_within(0.0f, statistics_variance(&s), 0.001f);
	zassert_equal(0, statistics_mean_rough(&s));
	zassert_equal(0, statistics_variance_rough(&s));
}

ZTEST(infuse_stats, test_small_numbers)
{
	/* From testcase_gen.py */
	const int32_t array_values[] = {18, 12, 24, 7,  3,  20, -2, 4,  30, 14,
					11, 9,  -2, 11, 27, 7,  16, 13, 5,  3};
	const float array_means[] = {18.000f, 15.000f, 18.000f, 15.250f, 12.800f, 14.000f, 11.714f,
				     10.750f, 12.889f, 13.000f, 12.818f, 12.500f, 11.385f, 11.357f,
				     12.400f, 12.062f, 12.294f, 12.333f, 11.947f, 11.500f};
	const float array_vars[] = {0.000f,  18.000f,  36.000f,  54.250f, 70.700f, 65.200f, 90.905f,
				    85.357f, 115.861f, 103.111f, 93.164f, 85.909f, 94.923f, 87.632f,
				    97.686f, 92.996f,  88.096f,  82.941f, 81.164f, 80.895f};

	run_test_case(array_values, array_means, array_vars, ARRAY_SIZE(array_values));
}

ZTEST(infuse_stats, test_medium_numbers)
{
	const int32_t array_values[] = {43918, 43770, 44329, 43522, 44038, 42123, 42224,
					42704, 42191, 43489, 42718, 43157, 44026, 42036,
					42772, 43420, 43869, 43368, 43122, 43051};
	const float array_means[] = {43918.000f, 43844.000f, 44005.667f, 43884.750f, 43915.400f,
				     43616.667f, 43417.714f, 43328.500f, 43202.111f, 43230.800f,
				     43184.182f, 43181.917f, 43246.846f, 43160.357f, 43134.467f,
				     43152.312f, 43194.471f, 43204.111f, 43199.789f, 43192.350f};
	const float array_vars[] = {0.000f,      10952.000f,  83884.333f,  114406.250f,
				    90501.800f,  607851.067f, 783616.905f, 735345.143f,
				    787194.361f, 707958.844f, 661068.764f, 601033.174f,
				    605752.974f, 663881.478f, 626516.124f, 589843.963f,
				    583192.890f, 550560.340f, 520328.509f, 494049.713f};

	run_test_case(array_values, array_means, array_vars, ARRAY_SIZE(array_values));
}

ZTEST(infuse_stats, test_large_numbers)
{
	const int32_t array_values[] = {-998969, -998578, -998251, -998799, -999000,
					-998925, -998449, -998525, -998458, -998726,
					-998368, -998399, -998450, -998345, -998216,
					-998093, -998802, -998385, -998042, -998098};
	const float array_means[] = {-998969.000f, -998773.500f, -998599.333f, -998649.250f,
				     -998719.400f, -998753.667f, -998710.143f, -998687.000f,
				     -998661.556f, -998668.000f, -998640.727f, -998620.583f,
				     -998607.462f, -998588.714f, -998563.867f, -998534.438f,
				     -998550.176f, -998541.000f, -998514.737f, -998493.900f};
	const float array_vars[] = {0.000f,     76440.500f, 129222.333f, 96114.917f, 96691.300f,
				    84398.267f, 83592.143f, 75935.143f,  72270.028f, 64655.333f,
				    66371.618f, 65207.174f, 62011.603f,  62161.912f, 66982.838f,
				    76374.529f, 75812.279f, 72868.471f,  81925.538f, 86297.147f};

	run_test_case(array_values, array_means, array_vars, ARRAY_SIZE(array_values));
}

ZTEST_SUITE(infuse_stats, NULL, NULL, NULL, NULL, NULL);
