/**
 * @file
 * @brief Infuse-IoT constrained binary patching
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * CPatch is a binary diff and patching algorithm designed for simple and
 * sequential output construction for constrained embedded devices. No caching,
 * single pass, optimized for executable binary files.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_PATCH_BINARY_H_
#define INFUSE_SDK_INCLUDE_INFUSE_PATCH_BINARY_H_

#include <stdint.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/stream_flash.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief cpatch patching API
 * @defgroup cpatch_patch_apis cpatch patching APIs
 * @{
 */

/** Magic number that signifies a CPatch file */
#define CPATCH_MAGIC_NUMBER 0xBA854092

/** Expected values for various memory regions */
struct cpatch_array_validation {
	/* Length of the memory region in bytes */
	uint32_t length;
	/* CRC32-IEEE of the memory region */
	uint32_t crc;
} __packed;

/** CPatch file header */
struct cpatch_header {
	/* Expected to match @ref CPATCH_MAGIC_NUMBER */
	uint32_t magic_value;
	/* Major version number of CPatch algorithm this file was generated for */
	uint8_t version_major;
	/* Minor version number of CPatch algorithm this file was generated for */
	uint8_t version_minor;
	/* Input file validation */
	struct cpatch_array_validation input_file;
	/* Output file validation */
	struct cpatch_array_validation output_file;
	/* Patch data validation */
	struct cpatch_array_validation patch_file;
	/* CRC32-IEEE of the preceding data in the header */
	uint32_t header_crc;
} __packed;

/**
 * @brief Patching output progress callback
 *
 * @note The frequency and offsets of the callback progress depend
 *       on the patch file contents.
 *
 * @param output_offset Current patching progress
 */
typedef void (*cpatch_progress_cb_t)(size_t output_offset);

/**
 * @brief Validate patch file and input data region
 *
 * Ensures that the patch file is internally consistent, and that it can
 * be applied to the flash area @a input. Returns the patch header so that
 * the caller can prepare the output region before starting the application
 * process.
 *
 * @param input Input file flash area
 * @param patch Patch file flash area
 * @param header Output storage for the patch header
 *
 * @retval 0 on success
 * @retval -EINVAL on patch file or input data validation errors
 * @retval -errno other error from @a flash_area_crc32
 */
int cpatch_patch_start(const struct flash_area *input, const struct flash_area *patch,
		       struct cpatch_header *header);

/**
 * @brief Create an output file by applying a patch file to an input file
 *
 * @note If `STREAM_FLASH_ERASE` is not enabled, the output flash area must be
 *       manually erased prior to calling this function.
 *
 * @param input Input file flash area
 * @param patch Patch file flash area
 * @param output Output stream flash context
 * @param header Patch header read by @ref cpatch_patch_start
 * @param progress_cb Optional progress callback
 *
 * @retval 0 on success
 * @retval -EINVAL on input or output validation errors
 * @retval -errno other negative error on flash read or write failures
 */
int cpatch_patch_apply(const struct flash_area *input, const struct flash_area *patch,
		       struct stream_flash_ctx *output, struct cpatch_header *header,
		       cpatch_progress_cb_t progress_cb);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_PATCH_BINARY_H_ */
