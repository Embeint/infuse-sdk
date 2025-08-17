/**
 * @file
 * @brief Hamming error correcting codes
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_MATH_HAMMING_H_
#define INFUSE_SDK_INCLUDE_INFUSE_MATH_HAMMING_H_

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hamming error correction codes API
 * @defgroup hamming_apis Hamming error correcting codes APIs
 * @{
 */

/**
 * @brief Encode a byte buffer using a (8,4) Hamming code
 *
 * Output byte 0 is the result of encoding the top 4 bits of input byte 0.
 * Output byte 1 is the result of encoding the bottom 4 bits of input byte 0.
 *
 * @param input Input data buffer (Original data)
 * @param input_len Length of input data buffer in bytes
 * @param output Output data buffer (Encoded data)
 * @param output_len Length of output data buffer in bytes (must be at least 2x input_len)
 *
 * @retval -EINVAL Invalid buffer sizes
 * @retval >=0 Number of output bytes generated
 */
int hamming_8_4_encode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len);

/**
 * @brief Decode a (8,4) Hamming encoded byte buffer
 *
 * This function will automatically correct single bit errors.
 * Decoding will terminate upon the first double bit error.
 *
 * @warning Odd length inputs will be truncated to even to ensure byte chunk outputs.
 *
 * @param input Input data buffer (Encoded data)
 * @param input_len Length of input data buffer in bytes
 * @param output Output data buffer (Original data)
 * @param output_len Length of output data buffer in bytes (must be at least 2x input_len)
 *
 * @retval -EINVAL Invalid buffer sizes
 * @retval >=0 Number of output data symbols generated (2 per byte)
 */
int hamming_8_4_decode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_HAMMING_H_ */
