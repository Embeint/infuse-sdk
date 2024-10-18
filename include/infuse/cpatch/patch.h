/**
 * @file
 * @brief Infuse-IoT constrained binary patching
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * CPatch is a binary diff and patching algorithm designed for simple and
 * sequential output construction for constrained embedded devices. No caching,
 * single pass, optimized for executable binary files.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_PATCH_BINARY_H_
#define INFUSE_SDK_INCLUDE_INFUSE_PATCH_BINARY_H_

#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/stream_flash.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief cpatch patching API
 * @defgroup cpatch_patch_apis cpatch patching APIs
 * @{
 */

/**
 * @brief Create an output file by applying a patch file to an input file
 *
 * @param input Input file flash area
 * @param patch Patch file flash area
 * @param output Output stream flash context
 *
 * @retval 0 on success
 * @retval -EINVAL on input or output validation errors
 * @retval -errno other negative error on flash read or write failures
 */
int cpatch_patch_file(const struct flash_area *input, const struct flash_area *patch,
		      struct stream_flash_ctx *output);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_PATCH_BINARY_H_ */
