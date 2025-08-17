/**
 * @file
 * @brief Internal diff functions
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_TDF_DIFFS_H_
#define INFUSE_SDK_SUBSYS_TDF_DIFFS_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Can a diff be generated between @a current and @a next
 *
 * @param tdf_len Total length of the TDF in byte
 * @param current Pointer to current TDF
 * @param next Pointer to next TDF
 *
 * @retval true diff can be generated
 * @retval false diff cannot be generated
 */
typedef bool (*tdf_diff_check_t)(int tdf_len, const void *current, const void *next);

/**
 * @brief Generate the diff between @a current and @a next
 *
 * Because TDF diffs are only enabled for homogeneous arrays, there is no requirement
 * that these functions are called on a single TDF at a time.
 *
 * @note Functions assume that the generated diffs are valid
 *
 * @param num_elements Number of fields to generate diffs for
 * @param current Pointer to current TDF
 * @param next Pointer to next TDF
 * @param out Diff output storage
 */
typedef void (*tdf_diff_encode_t)(int num_elements, const void *current, const void *next,
				  void *out);

/**
 * @brief Reconstruct an original TDF from a base + diff array
 *
 * @param tdf_len Length of the output TDF
 * @param base Base TDF data
 * @param out Output location for reconstructed TDF
 * @param diffs Pointer to diff value
 */
typedef void (*tdf_diff_apply_t)(uint8_t tdf_len, const void *base, void *out, void *diffs);

/** @ref tdf_diff_check_t for 8 bit diffs on 16 bit fields */
bool tdf_diff_check_16_8(int tdf_len, const void *current, const void *next);
/** @ref tdf_diff_encode_t for 8 bit diffs on 16 bit fields */
void tdf_diff_encode_16_8(int num_fields, const void *current, const void *next, void *out);
/** @ref tdf_diff_apply_t for 8 bit diffs on 16 bit fields */
void tdf_diff_apply_16_8(uint8_t tdf_len, const void *base, void *out, void *diffs);

/** @ref tdf_diff_check_t for 8 bit diffs on 32 bit fields */
bool tdf_diff_check_32_8(int tdf_len, const void *current, const void *next);
/** @ref tdf_diff_encode_t for 8 bit diffs on 32 bit fields */
void tdf_diff_encode_32_8(int num_fields, const void *current, const void *next, void *out);
/** @ref tdf_diff_apply_t for 8 bit diffs on 32 bit fields */
void tdf_diff_apply_32_8(uint8_t tdf_len, const void *base, void *out, void *diffs);

/** @ref tdf_diff_check_t for 16 bit diffs on 32 bit fields */
bool tdf_diff_check_32_16(int tdf_len, const void *current, const void *next);
/** @ref tdf_diff_encode_t for 16 bit diffs on 32 bit fields */
void tdf_diff_encode_32_16(int num_fields, const void *current, const void *next, void *out);
/** @ref tdf_diff_apply_t for 16 bit diffs on 32 bit fields */
void tdf_diff_apply_32_16(uint8_t tdf_len, const void *base, void *out, void *diffs);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_TDF_DIFFS_H_ */
