/**
 * @file
 * @brief Data filtering library
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_MATH_FILTER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_MATH_FILTER_H_

#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief filter API
 * @defgroup filter_apis filter APIs
 * @{
 */

/**
 * @brief Convert a filter alpha to a form compatible with IIR filters
 *
 * @param alpha Filter alpha value, between 0.0f and 1.0f exclusive.
 *
 * @return uint32_t alpha value suitable for IIR filters
 */
static inline uint32_t iir_filter_alpha_init(float alpha)
{
	return (uint32_t)(alpha * (1ULL << 32));
}

/**
 * @brief Signed 16-bit IIR single-pole filter with fraction saving
 *
 * Fraction saving is a noise shaping method that ensures that the
 * output of a DC input eventually equals that input.
 */
struct iir_filter_single_pole_s16 {
	int16_t y_prev;
	uint16_t alpha;
	uint16_t error;
};

/**
 * @brief Initialise the integer IIR single-pole filter
 *
 * @note Use @ref iir_filter_alpha_init to initialise alpha from a float
 *
 * @param filter Filter state
 * @param alpha Filter time constant (α=1−e^(−Δt/RC)), 0-1 scaled by (1 << 32)
 * @param initial Initial value for filter output
 */
static inline void iir_filter_single_pole_s16_init(struct iir_filter_single_pole_s16 *filter,
						   uint32_t alpha, int16_t initial)
{
	__ASSERT_NO_MSG(alpha != 0);
	filter->y_prev = initial;
	filter->alpha = alpha >> 16;
	filter->error = 0;
}

/**
 * @brief Run the filter for one step
 *
 * @param filter Filter state
 * @param x Filter input value
 *
 * @return int32_t Output value of the filter
 */
static inline int16_t iir_filter_single_pole_s16_step(struct iir_filter_single_pole_s16 *filter,
						      int16_t x)
{
	uint32_t one = (1U << 16);
	uint32_t alpha32 = filter->alpha;
	uint32_t alpha32_inv = one - alpha32;
	int32_t y_scaled = (alpha32 * x) + (alpha32_inv * filter->y_prev) + filter->error;
	int16_t y = (y_scaled >> 16);

	filter->error = y_scaled & UINT16_MAX;
	filter->y_prev = y;
	return y;
}

/**
 * @brief Signed 32-bit IIR single-pole filter with fraction saving
 *
 * Fraction saving is a noise shaping method that ensures that the
 * output of a DC input eventually equals that input.
 */
struct iir_filter_single_pole_s32 {
	int32_t y_prev;
	uint32_t alpha;
	uint32_t error;
};

/**
 * @brief Initialise the integer IIR single-pole filter
 *
 * @note Use @ref iir_filter_alpha_init to initialise alpha from a float
 *
 * @param filter Filter state
 * @param alpha Filter time constant (α=1−e^(−Δt/RC)), 0-1 scaled by (1 << 32)
 * @param initial Initial value for filter output
 */
static inline void iir_filter_single_pole_s32_init(struct iir_filter_single_pole_s32 *filter,
						   uint32_t alpha, int32_t initial)
{
	__ASSERT_NO_MSG(alpha != 0);
	filter->y_prev = initial;
	filter->alpha = alpha;
	filter->error = 0;
}

/**
 * @brief Run the filter for one step
 *
 * @param filter Filter state
 * @param x Filter input value
 *
 * @return int32_t Output value of the filter
 */
static inline int32_t iir_filter_single_pole_s32_step(struct iir_filter_single_pole_s32 *filter,
						      int32_t x)
{
	uint64_t one = (1ULL << 32);
	uint64_t alpha64 = filter->alpha;
	uint64_t alpha64_inv = one - alpha64;
	int64_t y_scaled = (alpha64 * x) + (alpha64_inv * filter->y_prev) + filter->error;
	int32_t y = (y_scaled >> 32);

	filter->error = y_scaled & UINT32_MAX;
	filter->y_prev = y;
	return y;
}

/**
 * @brief Floating point IIR single-pole filter
 */
struct iir_filter_single_pole_f32 {
	float y_prev;
	float alpha;
	float inv_alpha;
};

/**
 * @brief Initialise the float IIR single-pole filter
 *
 * @param filter Filter state
 * @param alpha Filter time constant
 * @param initial Initial value for filter output
 */
static inline void iir_filter_single_pole_f32_init(struct iir_filter_single_pole_f32 *filter,
						   float alpha, float initial)
{
	__ASSERT_NO_MSG((alpha > 0.0f) && (alpha < 1.0f));
	filter->y_prev = initial;
	filter->alpha = alpha;
	filter->inv_alpha = 1.0f - alpha;
}

/**
 * @brief Run the filter for one step
 *
 * @param filter Filter state
 * @param x Filter input value
 *
 * @return float Output value of the filter
 */
static inline float iir_filter_single_pole_f32_step(struct iir_filter_single_pole_f32 *filter,
						    float x)
{
	float y = (filter->alpha * x) + (filter->inv_alpha * filter->y_prev);

	filter->y_prev = y;
	return y;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_FILTER_H_ */
