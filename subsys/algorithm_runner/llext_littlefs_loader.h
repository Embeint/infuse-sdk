/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER_H_
#define INFUSE_SDK_SUBSYS_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER_H_

#include <stdint.h>

#include <zephyr/llext/llext.h>

#include <infuse/algorithms/implementation.h>

/**
 * @brief Load an algorithm extension from the Infuse LittleFS filesystem
 *
 * Loads @p file from @ref INFUSE_LFS_FOLDER_ALGORITHMS and finds its exported
 * `algorithm_config` symbol. The exported algorithm ID must match @p file.
 *
 * On success, the caller owns the returned extension reference and must call
 * @ref llext_unload when it is no longer required. The algorithm pointer remains
 * valid only while the extension is loaded.
 *
 * @param[in] file Algorithm file identifier
 * @param[out] extension Loaded LLEXT instance
 * @param[out] algorithm Exported algorithm configuration
 *
 * @retval 0 Algorithm loaded and validated
 * @retval -EBUSY Another LittleFS file is currently open
 * @retval -EINVAL The extension does not export a matching algorithm configuration
 * @return Negative error code from the LittleFS or LLEXT subsystems
 */
int llext_littlefs_load(uint32_t file, struct llext **extension,
			const struct infuse_algorithm **algorithm);

#endif /* INFUSE_SDK_SUBSYS_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER_H_ */
