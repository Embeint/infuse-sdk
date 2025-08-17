/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/math/cartesian.h>

ZTEST(in_circle, test_in_circle)
{
	struct cartesian_point_2d centre_origin = {0, 0};
	struct cartesian_point_2d centre_positive = {20000, 20000};
	struct cartesian_point_2d centre_negative = {-5000, -5000};

	zassert_true(
		cartesian_point_in_circle((struct cartesian_point_2d){0, 0}, centre_origin, 10));
	zassert_true(
		cartesian_point_in_circle((struct cartesian_point_2d){9, 0}, centre_origin, 10));
	zassert_true(
		cartesian_point_in_circle((struct cartesian_point_2d){-10, 0}, centre_origin, 10));
	zassert_true(
		cartesian_point_in_circle((struct cartesian_point_2d){0, -9}, centre_origin, 10));
	zassert_true(
		cartesian_point_in_circle((struct cartesian_point_2d){-7, -7}, centre_origin, 10));
	zassert_true(
		cartesian_point_in_circle((struct cartesian_point_2d){2, -3}, centre_origin, 10));

	zassert_false(
		cartesian_point_in_circle((struct cartesian_point_2d){11, 0}, centre_origin, 10));
	zassert_false(
		cartesian_point_in_circle((struct cartesian_point_2d){0, -11}, centre_origin, 10));
	zassert_false(
		cartesian_point_in_circle((struct cartesian_point_2d){8, 8}, centre_origin, 10));
	zassert_false(
		cartesian_point_in_circle((struct cartesian_point_2d){-9, -9}, centre_origin, 10));
	zassert_false(cartesian_point_in_circle((struct cartesian_point_2d){120, 454},
						centre_origin, 10));

	zassert_true(cartesian_point_in_circle((struct cartesian_point_2d){20000, 20000},
					       centre_positive, 500));
	zassert_true(cartesian_point_in_circle((struct cartesian_point_2d){20499, 20000},
					       centre_positive, 500));
	zassert_true(cartesian_point_in_circle((struct cartesian_point_2d){20102, 20302},
					       centre_positive, 500));
	zassert_false(cartesian_point_in_circle((struct cartesian_point_2d){20000, 21000},
						centre_positive, 500));
	zassert_false(cartesian_point_in_circle((struct cartesian_point_2d){18500, 19500},
						centre_positive, 500));

	zassert_true(cartesian_point_in_circle((struct cartesian_point_2d){-5000, -5000},
					       centre_negative, 100));
	zassert_true(cartesian_point_in_circle((struct cartesian_point_2d){-4901, -5000},
					       centre_negative, 100));
	zassert_true(cartesian_point_in_circle((struct cartesian_point_2d){-5020, -4987},
					       centre_negative, 100));
	zassert_false(cartesian_point_in_circle((struct cartesian_point_2d){-5000, -4000},
						centre_negative, 100));
	zassert_false(cartesian_point_in_circle((struct cartesian_point_2d){-5080, -4930},
						centre_negative, 100));
}

ZTEST_SUITE(in_circle, NULL, NULL, NULL, NULL, NULL);
