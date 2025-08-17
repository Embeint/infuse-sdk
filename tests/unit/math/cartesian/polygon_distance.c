/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/math/cartesian.h>

ZTEST(distance_to_polygon_2d, test_distance)
{
	/* Weird polygon */
	struct cartesian_point_2d weird[] = {{0, 0}, {10, 0}, {10, 10}, {10, 10}, {0, 10}};
	struct cartesian_point_2d middle = {5, 5};

	zassert_equal(5, cartesian_distance_to_polygon_edge(middle, weird, 5));
	zassert_equal(0, cartesian_distance_to_polygon_edge(middle, weird, 2));

	/* Point inside a square */
	struct cartesian_point_2d square[] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
	struct cartesian_point_2d point_inside1 = {5, 5};
	struct cartesian_point_2d point_inside2 = {7, 8};
	struct cartesian_point_2d point_inside3 = {1, 1};

	zassert_equal(5, cartesian_distance_to_polygon_edge(point_inside1, square, 4));
	zassert_equal(2, cartesian_distance_to_polygon_edge(point_inside2, square, 4));
	zassert_equal(1, cartesian_distance_to_polygon_edge(point_inside3, square, 4));

	struct cartesian_point_2d point_outside = {15, 5};
	struct cartesian_point_2d point_on_vertex = {0, 0};
	struct cartesian_point_2d point_on_edge = {5, 0};

	zassert_equal(5, cartesian_distance_to_polygon_edge(point_outside, square, 4));
	zassert_equal(0, cartesian_distance_to_polygon_edge(point_on_vertex, square, 4));
	zassert_equal(0, cartesian_distance_to_polygon_edge(point_on_edge, square, 4));

	/* Triangle */
	struct cartesian_point_2d triangle[] = {{0, 0}, {10, 0}, {5, 10}};
	struct cartesian_point_2d point_near_triangle = {6, 5};

	zassert_equal(2, cartesian_distance_to_polygon_edge(point_near_triangle, triangle, 3));

	/* Complex polygon */
	struct cartesian_point_2d complex_polygon[] = {{0, 0}, {10, 0}, {10, 10}, {5, 5}, {0, 10}};
	struct cartesian_point_2d point_near_complex = {7, 6};

	zassert_equal(1,
		      cartesian_distance_to_polygon_edge(point_near_complex, complex_polygon, 5));
}

ZTEST_SUITE(distance_to_polygon_2d, NULL, NULL, NULL, NULL, NULL);
