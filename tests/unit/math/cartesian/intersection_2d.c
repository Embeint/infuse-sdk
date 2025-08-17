/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/math/cartesian.h>

static void expect_intersection(struct cartesian_line_2d a, struct cartesian_line_2d b,
				struct cartesian_point_2d expected)
{
	struct cartesian_line_2d a_invert = {a.b, a.a};
	struct cartesian_line_2d b_invert = {b.b, b.a};
	struct cartesian_point_2d intersection;
	bool result;

	/* Test lines as provided, and various inversions */
	result = cartesian_line_intersection(a, b, &intersection);
	zassert_true(result);
	zassert_equal(expected.x, intersection.x);
	zassert_equal(expected.y, intersection.y);
	result = cartesian_line_intersection(a_invert, b, &intersection);
	zassert_true(result);
	zassert_equal(expected.x, intersection.x);
	zassert_equal(expected.y, intersection.y);
	result = cartesian_line_intersection(a, b_invert, &intersection);
	zassert_true(result);
	zassert_equal(expected.x, intersection.x);
	zassert_equal(expected.y, intersection.y);
	result = cartesian_line_intersection(a_invert, b_invert, &intersection);
	zassert_true(result);
	zassert_equal(expected.x, intersection.x);
	zassert_equal(expected.y, intersection.y);
}

ZTEST(intersection_2d, test_intersections)
{
	/* Two intersecting lines */
	expect_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			    (struct cartesian_line_2d){{0, 10}, {10, 0}},
			    (struct cartesian_point_2d){5, 5});

	/* Horizontal line intersecting vertical line  */
	expect_intersection((struct cartesian_line_2d){{0, 5}, {10, 5}},
			    (struct cartesian_line_2d){{5, 0}, {5, 10}},
			    (struct cartesian_point_2d){5, 5});

	/* Vertical line intersecting at the boundary  */
	expect_intersection((struct cartesian_line_2d){{0, 0}, {0, 10}},
			    (struct cartesian_line_2d){{0, 10}, {10, 10}},
			    (struct cartesian_point_2d){0, 10});

	/* Intersection at line segment endpoint  */
	expect_intersection((struct cartesian_line_2d){{0, 0}, {5, 5}},
			    (struct cartesian_line_2d){{5, 5}, {10, 0}},
			    (struct cartesian_point_2d){5, 5});

	/*  Origin intersection  */
	expect_intersection((struct cartesian_line_2d){{-5, -5}, {5, 5}},
			    (struct cartesian_line_2d){{-5, 5}, {5, -5}},
			    (struct cartesian_point_2d){0, 0});

	/* Negative intersection */
	expect_intersection((struct cartesian_line_2d){{0, 0}, {-10, -10}},
			    (struct cartesian_line_2d){{0, -10}, {-10, 0}},
			    (struct cartesian_point_2d){-5, -5});
}

static void expect_no_intersection(struct cartesian_line_2d a, struct cartesian_line_2d b)
{
	struct cartesian_line_2d a_invert = {a.b, a.a};
	struct cartesian_line_2d b_invert = {b.b, b.a};
	struct cartesian_point_2d intersection;
	bool result;

	/* Test lines as provided, and various inversions */
	result = cartesian_line_intersection(a, b, &intersection);
	zassert_false(result);
	result = cartesian_line_intersection(a_invert, b, &intersection);
	zassert_false(result);
	result = cartesian_line_intersection(a, b_invert, &intersection);
	zassert_false(result);
	result = cartesian_line_intersection(a_invert, b_invert, &intersection);
	zassert_false(result);
}

ZTEST(intersection_2d, test_no_intersections)
{
	/* Parallel lines (no intersection) */
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{0, 1}, {10, 11}});

	/* Collinear lines but no intersection within segments  */
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{15, 15}, {20, 20}});

	/* Lines intersect outside the segments  */
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {5, 5}},
			       (struct cartesian_line_2d){{6, 6}, {10, 10}});

	/* Point not on line */
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {0, 0}},
			       (struct cartesian_line_2d){{1, 1}, {2, 2}});

	/* Point on line */
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{5, 5}, {5, 5}});

	/* Same line segments (overlapping lines) */
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{0, 0}, {10, 10}});

	/* Various near misses */
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{-1, -100}, {-1, 10}});
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{11, -100}, {11, 10}});
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{-100, -1}, {100, -1}});
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{-100, 11}, {100, 11}});
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{10, 11}, {11, 10}});
	expect_no_intersection((struct cartesian_line_2d){{0, 0}, {10, 10}},
			       (struct cartesian_line_2d){{-1, 0}, {0, -1}});
	expect_no_intersection((struct cartesian_line_2d){{-10, -10}, {-5, -15}},
			       (struct cartesian_line_2d){{-100, -4}, {100, -4}});
	expect_no_intersection((struct cartesian_line_2d){{-10, -10}, {-5, -15}},
			       (struct cartesian_line_2d){{-100, -16}, {100, -16}});
	expect_no_intersection((struct cartesian_line_2d){{-10, -10}, {-5, -15}},
			       (struct cartesian_line_2d){{-16, -100}, {-16, 100}});
	expect_no_intersection((struct cartesian_line_2d){{-10, -10}, {-5, -15}},
			       (struct cartesian_line_2d){{-4, -100}, {-4, 100}});
	expect_no_intersection((struct cartesian_line_2d){{-10, -10}, {-5, -15}},
			       (struct cartesian_line_2d){{-11, -10}, {-10, -11}});
	expect_no_intersection((struct cartesian_line_2d){{-10, -10}, {-5, -15}},
			       (struct cartesian_line_2d){{-5, -16}, {-4, -15}});
}

ZTEST_SUITE(intersection_2d, NULL, NULL, NULL, NULL, NULL);
