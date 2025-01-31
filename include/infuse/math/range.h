/**
 * @file
 * @brief Infuse-IoT range helpers
 * @copyright 2025 Embeint Inc
 * @author Anton Schieber <anton@lodienabled.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_MATH_RANGE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_MATH_RANGE_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Range state. */
struct range_state {
	/** Minimum value. */
	int32_t min;
	/** Maximum value. */
	int32_t max;
};

/**
 * @brief Initializer for @ref range_state.
 */
#define INFUSE_RANGE_INIT()                                                                        \
	{                                                                                          \
		.min = (INT32_MIN),                                                                \
		.max = (INT32_MAX),                                                                \
	}

/**
 * @brief Resets the range calculation state.
 *
 * @param state Pointer to the `range_state` structure to reset.
 */
static inline void range_reset(struct range_state *state)
{
	state->min = INT32_MAX;
	state->max = INT32_MIN;
}

/**
 * @brief Update the range object with a new sample
 *
 * @param state Range state object
 * @param value New value to feed into state
 */
static inline void range_update(struct range_state *state, int32_t value)
{
	state->min = MIN(value, state->min);
	state->max = MAX(value, state->max);
}

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_RANGE_H_ */
