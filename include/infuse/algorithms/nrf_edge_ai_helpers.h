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

#ifdef __cplusplus
extern "C" {
#endif

#define INPUT_FEATURE_DATA_TYPE(type)      type##_INPUT_FEATURE_DATA_TYPE
#define INPUT_UNIQ_FEATURES_USED_NUM(type) type##_INPUT_UNIQ_FEATURES_USED_NUM
#define MODEL_OUTPUTS_NUM(type)            type##_MODEL_OUTPUTS_NUM
#define MODEL_OUTPUT_FORMATTER(type, output, class_idx)                                            \
	type##_MODEL_OUTPUT_FORMATTER(output, class_idx)

/**
 * @brief Get the float encoded probability ratio of a specific class from the model output struct.
 *
 * @param output Pointer to the model output structure.
 * @param class_idx Index of the class for which the probability is requested.
 * @return Probability ratio as a double.
 */
static inline double nrf_edge_ai_probability_f32(const nrf_edgeai_decoded_output_classif_t *output,
						 uint32_t class_idx)
{
	return output->probabilities.p_f32[class_idx];
}

/**
 * @brief Get the quantised uint8 encoded probability ratio of a specific class from the model
 * output struct.
 *
 * @param output Pointer to the model output structure.
 * @param class_idx Index of the class for which the probability is requested.
 * @return Probability ratio as a double.
 */
static inline double nrf_edge_ai_probability_q8(const nrf_edgeai_decoded_output_classif_t *output,
						uint32_t class_idx)
{
	return (double)output->probabilities.p_q8[class_idx] / (double)UINT8_MAX;
}

/**
 * @brief Get the quantised uint16 encoded probability ratio of a specific class from the model
 * output struct.
 *
 * @param output Pointer to the model output structure.
 * @param class_idx Index of the class for which the probability is requested.
 * @return Probability ratio as a double.
 */
static inline double nrf_edge_ai_probability_q16(const nrf_edgeai_decoded_output_classif_t *output,
						 uint32_t class_idx)
{
	return (double)output->probabilities.p_q16[class_idx] / (double)UINT16_MAX;
}

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_ALGORITHMS_NRF_EDGE_AI_HELPERS_H */
