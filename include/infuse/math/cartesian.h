/**
 * @file
 * @brief Cartesian geometery math library
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_MATH_CARTESIAN_H_
#define INFUSE_SDK_INCLUDE_INFUSE_MATH_CARTESIAN_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cartesian math API
 * @defgroup cartesian_apis Cartesian math APIs
 * @{
 */

/* Point in 2D space */
struct cartesian_point_2d {
	int32_t x;
	int32_t y;
};

/* Finite line in 2D space */
struct cartesian_line_2d {
	struct cartesian_point_2d a;
	struct cartesian_point_2d b;
};

/**
 * @brief Find the intersection point of two finite lines, if it exists
 *
 * @param a First finite line
 * @param b Second finite line
 * @param intersection Storage for intersection point
 *
 * @return true If finite lines intersect
 * @return false If finite lines do not intersect, or are collinear
 */
bool cartesian_line_intersection(struct cartesian_line_2d a, struct cartesian_line_2d b,
				 struct cartesian_point_2d *intersection);

/**
 * @brief Determine if a point in inside a circle
 *
 * @param point Point to test
 * @param origin Centre point of circle
 * @param radius Radius of circle
 *
 * @return true If point is inside circle
 * @return false If point is outside of circle
 */
bool cartesian_point_in_circle(struct cartesian_point_2d point, struct cartesian_point_2d origin,
			       uint32_t radius);

/**
 * @brief Determine if a point in inside an arbitrary polygon
 *
 * @param point Point to test
 * @param polygon Pointer to array of points defining the polygon (Do not duplicate first point)
 * @param vertices Number of points in polygon array
 *
 * @return true If point is inside polygon
 * @return false If point is outside of polygon
 */
bool cartesian_point_in_polygon(struct cartesian_point_2d point,
				const struct cartesian_point_2d *polygon, size_t vertices);

/**
 * @brief Determine the minimum distance to a polygons edge
 *
 * @param point Point to test
 * @param polygon Pointer to array of points defining the polygon (Do not duplicate first point)
 * @param vertices Number of points in polygon array
 *
 * @return uint32_t Distance to the nearest edge
 */
uint32_t cartesian_distance_to_polygon_edge(struct cartesian_point_2d point,
					    const struct cartesian_point_2d *polygon,
					    size_t vertices);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_CARTESIAN_H_ */
