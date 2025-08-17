/**
 * @file
 * @brief Infuse-IoT statistics helpers
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_MATH_STATISTICS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_MATH_STATISTICS_H_

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Running statistics API
 * @defgroup statistics_apis Infuse-IoT running statistics APIs
 *
 * This API is a C implementation of the following paper, which is itself
 * inspired by the well known Welford's algorithm:
 *     https://sfat.massey.ac.nz/research/centres/crisp/pdfs/2013_IVCNZ_214.pdf
 *
 * @{
 */

struct statistics_state {
	/* Variance approximation */
	int64_t v;
	/* Variance correction factor */
	int64_t w;
	/* Mean approximation */
	int32_t m;
	/* Mean fractional accumulation */
	int32_t p;
	/* Sequence count */
	uint32_t n;
};

/**
 * @brief Reset statistics object
 *
 * @param state Statistics state object to reset
 */
static inline void statistics_reset(struct statistics_state *state)
{
	memset(state, 0x00, sizeof(*state));
}

/**
 * @brief Update the statistics object with a new sample
 *
 * @param state Statistics state object
 * @param value New value to feed into state
 */
void statistics_update(struct statistics_state *state, int32_t value);

/**
 * @brief Compute the mean of the statistics object
 *
 * @param state Statistics state object
 *
 * @return float Mean of samples
 */
static inline float statistics_mean(const struct statistics_state *state)
{
	if (state->n == 0) {
		return 0.0f;
	}

	/* Equation 8 */
	return (float)state->m + ((float)state->p / state->n);
}

/**
 * @brief Compute the variance of the statistics object
 *
 * @param state Statistics state object
 *
 * @return float Variance of samples
 */
static inline float statistics_variance(const struct statistics_state *state)
{
	if (state->n < 2) {
		return 0.0f;
	}
	float mean_error = statistics_mean(state) - state->m;

	/* Equation 9 */
	return (float)state->v + ((float)state->w / (float)(state->n - 1)) -
	       (state->n * mean_error * mean_error / (state->n - 1));
}

/**
 * @brief Compute the rough mean of the statistics object
 *
 * The computed value is "rough" in the sense that it does not attempt to round
 * to the nearest whole number, and merely takes the integer portion.
 *
 * Tests validate that this value is within 1.0f of the value returned by
 * @ref statistics_mean.
 *
 * @param state Statistics state object
 *
 * @return uint32_t Approximate mean of samples
 */
static inline int32_t statistics_mean_rough(const struct statistics_state *state)
{
	return state->m;
}

/**
 * @brief Compute the rough variance of the statistics object
 *
 * The computed value is "rough" in the sense that it does not attempt to round
 * to the nearest whole number, and merely takes the integer portion. It also
 * does not take into account the difference between `state->m` and the true
 * mean, as described in the paper.
 *
 * Tests validate that this value is within 1.0f or 2% of the value returned by
 * @ref statistics_variance, whichever is less accurate.
 *
 * @param state Statistics state object
 *
 * @return uint64_t Approximate variance of samples
 */
static inline uint64_t statistics_variance_rough(const struct statistics_state *state)
{
	if (state->n < 2) {
		return 0;
	}
	return state->v + (state->w / (state->n - 1));
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_STATISTICS_H_ */
