/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/math/geodesy.h>

#include "arm_math.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* Earth's radius in meters */
#define EARTH_RADIUS_METERS 6371000
#define DEG_TO_RAD          (M_PI / 180.0f)
#define SCALE_FACTOR        10000000

static float32_t scaled_deg_to_rad(int32_t scaled_deg)
{
	return ((float32_t)scaled_deg / SCALE_FACTOR) * DEG_TO_RAD;
}

uint32_t geodesy_great_circle_distance(struct geodesy_coordinate p1, struct geodesy_coordinate p2)
{
	/* Convert latitude and longitude to radians */
	float32_t lat1_rad = scaled_deg_to_rad(p1.latitude);
	float32_t lon1_rad = scaled_deg_to_rad(p1.longitude);
	float32_t lat2_rad = scaled_deg_to_rad(p2.latitude);
	float32_t lon2_rad = scaled_deg_to_rad(p2.longitude);

	/* Haversine formula */
	float32_t sin_dlat_2, sin_dlon_2, cos_lat1, cos_lat2;
	float32_t dlat = lat2_rad - lat1_rad;
	float32_t dlon = lon2_rad - lon1_rad;

	sin_dlat_2 = arm_sin_f32(dlat * 0.5f);
	sin_dlon_2 = arm_sin_f32(dlon * 0.5f);
	cos_lat1 = arm_cos_f32(lat1_rad);
	cos_lat2 = arm_cos_f32(lat2_rad);

	float32_t a = sin_dlat_2 * sin_dlat_2 + cos_lat1 * cos_lat2 * sin_dlon_2 * sin_dlon_2;
	float32_t sqrt_a, sqrt_1_minus_a, atan2_result;

	arm_sqrt_f32(a, &sqrt_a);
	arm_sqrt_f32(1.0f - a, &sqrt_1_minus_a);
	arm_atan2_f32(sqrt_a, sqrt_1_minus_a, &atan2_result);

	float32_t distance = 2.0f * EARTH_RADIUS_METERS * atan2_result;

	/* Round to nearest meter */
	return (uint32_t)(distance + 0.5f);
}
