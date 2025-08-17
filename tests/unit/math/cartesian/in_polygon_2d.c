/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/math/cartesian.h>

ZTEST(in_polygon_2d, test_in_polygon)
{
	/* Point inside a triangle */
	struct cartesian_point_2d triangle[] = {{0, 0}, {10, 0}, {5, 10}};
	struct cartesian_point_2d point_inside = {5, 5};

	zassert_true(cartesian_point_in_polygon(point_inside, triangle, 3));

	/* Point outside a triangle */
	struct cartesian_point_2d point_outside = {15, 15};

	zassert_false(cartesian_point_in_polygon(point_outside, triangle, 3));

	/* Point on vertex of a triangle */
	struct cartesian_point_2d point_on_vertex = {0, 0};

	zassert_true(cartesian_point_in_polygon(point_on_vertex, triangle, 3));

	/* Point on edge of a triangle */
	struct cartesian_point_2d point_on_edge = {5, 0};

	zassert_true(cartesian_point_in_polygon(point_on_edge, triangle, 3));

	/* Polygon with less than 3 vertices */
	struct cartesian_point_2d invalid_polygon[] = {{0, 0}, {10, 10}};

	zassert_false(cartesian_point_in_polygon(point_inside, invalid_polygon, 2));

	/* Complex concave polygon */
	struct cartesian_point_2d concave_polygon[] = {{0, 0}, {10, 0}, {10, 10}, {5, 5}, {0, 10}};
	struct cartesian_point_2d point_in_concave = {3, 3};

	zassert_true(cartesian_point_in_polygon(point_in_concave, concave_polygon, 5));

	/* Point outside complex concave polygon */
	struct cartesian_point_2d point_outside_concave = {7, 8};

	zassert_false(cartesian_point_in_polygon(point_outside_concave, concave_polygon, 5));

	/* Point on horizontal edge */
	struct cartesian_point_2d point_on_horizontal = {5, 0};

	zassert_true(cartesian_point_in_polygon(point_on_horizontal, concave_polygon, 5));

	/* Point on vertical edge */
	struct cartesian_point_2d point_on_vertical = {10, 5};

	zassert_true(cartesian_point_in_polygon(point_on_vertical, concave_polygon, 5));

	/* Point on angled */
	struct cartesian_point_2d point_on_angled = {7, 7};

	zassert_true(cartesian_point_in_polygon(point_on_angled, concave_polygon, 5));

	/* Ray passes through vertex */
	struct cartesian_point_2d vertex_polygon[] = {{0, 0}, {10, 10}, {20, 0}};
	struct cartesian_point_2d point_aligned_with_vertex = {5, 5};

	zassert_true(cartesian_point_in_polygon(point_aligned_with_vertex, vertex_polygon, 3));

	/* Ray passes through multiple vertices */
	struct cartesian_point_2d multi_vertex_polygon[] = {{0, 0},   {10, 0}, {10, 10},
							    {20, 10}, {20, 0}, {30, 0}};
	struct cartesian_point_2d point_multi_vertex = {15, 0};

	zassert_true(cartesian_point_in_polygon(point_multi_vertex, multi_vertex_polygon, 6));

	/* Point at far right of polygon */
	struct cartesian_point_2d far_right_point = {30, 5};

	zassert_false(cartesian_point_in_polygon(far_right_point, multi_vertex_polygon, 6));

	/* Complex concave polygon in negative space */
	struct cartesian_point_2d negative_concave_polygon[] = {
		{-10, -10}, {-5, -5}, {0, -10}, {5, -5}, {10, -10}, {10, 0}, {0, 5}, {-10, 0}};
	struct cartesian_point_2d point_in_negative_concave = {0, 0};

	zassert_true(
		cartesian_point_in_polygon(point_in_negative_concave, negative_concave_polygon, 8));

	/* Point outside negative concave polygon */
	struct cartesian_point_2d point_outside_negative_concave = {-8, -9};

	zassert_false(cartesian_point_in_polygon(point_outside_negative_concave,
						 negative_concave_polygon, 8));

	/* Point on edge of negative concave polygon */
	struct cartesian_point_2d point_on_edge_negative = {-10, -5};

	zassert_true(
		cartesian_point_in_polygon(point_on_edge_negative, negative_concave_polygon, 8));

	/* Point on vertex of negative concave polygon */
	struct cartesian_point_2d point_on_vertex_negative = {-10, -10};

	zassert_true(
		cartesian_point_in_polygon(point_on_vertex_negative, negative_concave_polygon, 8));

	struct cartesian_point_2d complex_polygon[] = {{0, 0}, {10, 0}, {10, 10}, {5, 5}, {0, 10}};
	struct cartesian_point_2d point_near_edge = {9, 8};

	zassert_true(cartesian_point_in_polygon(point_near_edge, complex_polygon, 5));

	/* This point is just outside the polygon, near the same edge */
	struct cartesian_point_2d point_outside_near_edge = {11, 8};

	zassert_false(cartesian_point_in_polygon(point_outside_near_edge, complex_polygon, 5));
}

ZTEST_SUITE(in_polygon_2d, NULL, NULL, NULL, NULL, NULL);
