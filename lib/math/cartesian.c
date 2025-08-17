/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <infuse/math/cartesian.h>
#include <infuse/math/common.h>

int64_t determinant(int64_t x1, int64_t y_1, int64_t x2, int64_t y2)
{
	return (x1 * y2) - (y_1 * x2);
}

bool cartesian_line_intersection(struct cartesian_line_2d line1, struct cartesian_line_2d line2,
				 struct cartesian_point_2d *intersection)
{
	/* y_1 is used as y1 is the posix bessel function */
	int64_t x1 = line1.a.x, y_1 = line1.a.y;
	int64_t x2 = line1.b.x, y2 = line1.b.y;
	int64_t x3 = line2.a.x, y3 = line2.a.y;
	int64_t x4 = line2.b.x, y4 = line2.b.y;
	int64_t det, t_num, u_num;

	det = determinant(x1 - x2, y_1 - y2, x3 - x4, y3 - y4);

	if (det == 0) {
		/* Lines are parallel or coincident */
		return false;
	}

	t_num = determinant(x1 - x3, y_1 - y3, x3 - x4, y3 - y4);
	u_num = determinant(x1 - x3, y_1 - y3, x1 - x2, y_1 - y2);

	/* Check if intersection point is outside the line segments.
	 * Code is validating the fraction (t_num / det) and (u_num / det) lies in the range [0, 1].
	 */
	if (det > 0) {
		if (t_num < 0 || t_num > det || u_num < 0 || u_num > det) {
			return false;
		}
	} else {
		if (t_num > 0 || t_num < det || u_num > 0 || u_num < det) {
			return false;
		}
	}

	/* Calculate intersection point */
	intersection->x = x1 + ((x2 - x1) * t_num) / det;
	intersection->y = y_1 + ((y2 - y_1) * t_num) / det;
	return true;
}

bool cartesian_point_in_circle(struct cartesian_point_2d point, struct cartesian_point_2d centre,
			       uint32_t radius)
{
	uint64_t radius_squared = (uint64_t)radius * radius;
	uint64_t dist_squared;

	/* Pretend the circle center is at (0,0) */
	point.x -= centre.x;
	point.y -= centre.y;

	dist_squared = (point.x * point.x) + (point.y * point.y);

	return dist_squared <= radius_squared;
}

bool cartesian_point_in_polygon(struct cartesian_point_2d point,
				const struct cartesian_point_2d *polygon, size_t vertices)
{
	/* Ray from test point to "infinity" */
	const struct cartesian_line_2d ray = {point, {INT32_MAX, point.y}};
	struct cartesian_point_2d intersection;
	int intersections = 0;

	if (vertices < 3) {
		/* Polygon requires at least 3 verticies */
		return false;
	}

	for (size_t i = 0; i < vertices; i++) {
		size_t j = (i + 1) % vertices;
		struct cartesian_line_2d edge = {polygon[i], polygon[j]};

		if (cartesian_line_intersection(ray, edge, &intersection)) {

			/* Check if the intersection is to the right of or at the test point */
			if (intersection.x >= point.x) {

				if (intersection.x == point.x && intersection.y == point.y) {
					/* The point is on a vertex */
					return true;
				}

				/* Handle edge cases */
				if (edge.a.y == point.y || edge.b.y == point.y) {
					/* If the intersection is at a vertex, count it
					 * only, if the other endpoint is below the ray
					 */
					if ((edge.a.y > point.y && edge.b.y <= point.y) ||
					    (edge.b.y > point.y && edge.a.y <= point.y)) {
						intersections++;
					}
				} else {
					intersections++;
				}
			}
		}
	}

	/* If the number of intersections is odd, the point is inside the polygon */
	return (intersections % 2) == 1;
}

int64_t squared_distance(struct cartesian_point_2d p1, struct cartesian_point_2d p2)
{
	int64_t dx = (int64_t)p1.x - p2.x;
	int64_t dy = (int64_t)p1.y - p2.y;

	return dx * dx + dy * dy;
}

int64_t squared_distance_to_line(struct cartesian_point_2d p, struct cartesian_point_2d a,
				 struct cartesian_point_2d b)
{
	int64_t l2 = squared_distance(a, b);

	if (l2 == 0) {
		/* The line segment is actually a point */
		return squared_distance(p, a);
	}

	/* Consider the line extending the segment, parameterized as a + t (b - a)
	 * We find projection of point p onto the line
	 * It falls where t = [(p-a) . (b-a)] / |b-a|^2
	 */
	int64_t t = ((int64_t)(p.x - a.x) * (b.x - a.x) + (int64_t)(p.y - a.y) * (b.y - a.y)) *
		    100 / l2;

	if (t < 0) {
		/* Beyond the 'a' end of the segment */
		return squared_distance(p, a);
	} else if (t > 100) {
		/* Beyond the 'b' end of the segment */
		return squared_distance(p, b);
	}

	struct cartesian_point_2d projection = {(int32_t)(a.x + t * (b.x - a.x) / 100),
						(int32_t)(a.y + t * (b.y - a.y) / 100)};

	return squared_distance(p, projection);
}

uint32_t cartesian_distance_to_polygon_edge(struct cartesian_point_2d point,
					    const struct cartesian_point_2d *polygon,
					    size_t vertices)
{
	if (vertices < 3) {
		/* Invalid polygon */
		return 0;
	}

	int64_t min_squared_distance = INT64_MAX;

	for (size_t i = 0; i < vertices; i++) {
		size_t j = (i + 1) % vertices;
		int64_t squared_dist = squared_distance_to_line(point, polygon[i], polygon[j]);

		if (squared_dist < min_squared_distance) {
			min_squared_distance = squared_dist;
		}
	}

	return math_sqrt64((uint64_t)min_squared_distance);
}
