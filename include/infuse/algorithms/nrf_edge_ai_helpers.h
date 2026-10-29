/**
 * @file nrf_edge_ai_helpers.h
 * @brief nRF Edge AI algorithm specific helper functions.
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_ALGORITHMS_NRF_EDGE_AI_HELPERS_H
#define INFUSE_ALGORITHMS_NRF_EDGE_AI_HELPERS_H

#include <stdint.h>

#include <nrf_edgeai/rt/nrf_edgeai_output_types.h>

#include "model_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract the classification probability as a double precision floating point number.
 *
 * @param probability classification output object.
 * @param class_idx Index of the class for which to extract the probability.
 * @return Probability ratio as a double (0-1).
 */
static inline double
get_probability_as_double(const nrf_edgeai_decoded_output_classif_t *probability,
			  uint32_t class_idx)
{
#if defined(MODEL_OUTPUT_TYPE_FLOAT)
	return probability->probabilities.p_f32[class_idx];
#elif defined(MODEL_OUTPUT_TYPE_Q8)
	/* Quantized values are interpreted as percentages: 0 - 1. */
	return (double)probability->probabilities.p_q8[class_idx] / (float)UINT8_MAX;
#elif defined(MODEL_OUTPUT_TYPE_Q16)
	/* Quantized values are interpreted as percentages: 0 - 1. */
	return (double)probability->probabilities.p_q16[class_idx] / (float)UINT16_MAX;
#else
#error "Unsupported model output type"
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_ALGORITHMS_NRF_EDGE_AI_HELPERS_H */
