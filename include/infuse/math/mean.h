/**
 * @file
 * @brief Infuse-IoT mean helpers
 * @copyright 2025 Embeint Inc
 * @author Anton Schieber <anton@lodienabled.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_MATH_ < MEAN_H_
#define INFUSE_SDK_INCLUDE_INFUSE_MATH_ < MEAN_H_

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Running average API
 * @defgroup average_apis Infuse-IoT running mean APIs
 *
 * This API is a C implementation of the following article, which is itself
 * inspired by the well known Welford's algorithm:
 *     https://www.johndcook.com/blog/standard_deviation/
 * 	   https://stackoverflow.com/a/17637351
 *
 * @{
 */

struct mean_state {
	/* Mean */
	int32_t m;
	/* Sequence count */
	uint32_t n;
};

/**
 * @brief Update the mean object with a new sample
 *
 * @param state Mean state object
 * @param value New value to feed into state
 *
 * @note As the number of samples (`n`) increases, the potential for numerical error
 *       in the mean calculation also increases. This is due to the accumulation of
 *       rounding errors in the division operation `(value - m) / n`.
 */
void mean_update(struct mean_state *state, int32_t value);

/**
 * @brief Compute the mean of the mean object
 *
 * @param state Mean state object
 *
 * @return float Mean of samples
 */
static inline int32_t mean_calculate(struct mean_state *state)
{
	if (state->n == 0) {
		return 0;
	}
	return state->m;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_<MEAN_H_ */
