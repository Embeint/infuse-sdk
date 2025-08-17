/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <infuse/math/geodesy.h>

ZTEST(geodesy, test_cities_north_america)
{
	struct geodesy_coordinate locations[] = {
		/* New York City */
		{407140000, -739866667},
		/* Los Angeles */
		{340522222, -1182434444},
		/* Chicago */
		{418811111, -876772222},
		/* Houston */
		{298006111, -954011111},
		/* Phoenix */
		{333689444, -1120754167},
	};

	/* 10km on the scale of cities */
	zassert_within(3936000, geodesy_great_circle_distance(locations[0], locations[1]), 10000,
		       "NY to LA");
	zassert_within(1145000, geodesy_great_circle_distance(locations[0], locations[2]), 10000,
		       "NY to Chicago");
	zassert_within(2288000, geodesy_great_circle_distance(locations[0], locations[3]), 10000,
		       "NY to Houston");
	zassert_within(3448000, geodesy_great_circle_distance(locations[0], locations[4]), 10000,
		       "NY to Phoenix");
	zassert_within(1519000, geodesy_great_circle_distance(locations[2], locations[3]), 10000,
		       "Chicago to Houston");
}

ZTEST(geodesy, test_close_australia)
{
	/* Random coordinates in Brisbane determined from Google Earth Pro */
	zassert_within(
		5000,
		geodesy_great_circle_distance((struct geodesy_coordinate){-274643470, 1529580410},
					      (struct geodesy_coordinate){-274961750, 1529222800}),
		5, "5km");
	zassert_within(
		25000,
		geodesy_great_circle_distance((struct geodesy_coordinate){-277365370, 1529736950},
					      (struct geodesy_coordinate){-275932310, 1531692160}),
		10, "25km");
}

ZTEST_SUITE(geodesy, NULL, NULL, NULL, NULL, NULL);
