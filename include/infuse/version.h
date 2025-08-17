/**
 * @file
 * @brief Infuse application versioning
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VERSION_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VERSION_H_

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>

#if __has_include("zephyr/app_version.h")
#include "zephyr/app_version.h"
#else
/* Fallback for applications without VERSION file */
#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 0
#define APP_PATCHLEVEL    0
#define APP_TWEAK         0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse version API
 * @defgroup infuse_version_apis version APIs
 * @{
 */

/* Use MCUboot semantic version definitions */
#define infuse_version mcuboot_img_sem_ver

/**
 * @brief Convert version struct to sortable integer
 *
 * @param v struct infuse_version pointer
 *
 * @retval uint32_t equivalent integer
 */
#define INFUSE_VERSION_INT(v)                                                                      \
	((((uint32_t)(v)->major) << 24) | (((uint32_t)(v)->minor) << 16) |                         \
	 ((uint32_t)(v)->revision))

/**
 * @brief Get version of the currently running application
 *
 * @returns Current application version
 */
struct infuse_version application_version_get(void);

/**
 * @brief Compare two version structures
 *
 * Return value follows the convention of the C library `qsort` function.
 *
 * @note The `build_num` field is ignored for comparison purposes.
 *
 * @param a First version to compare
 * @param b Second version to compare
 *
 * @retval 1 @a a is an earlier version than @a b
 * @retval -1 @a a is a later version than @a b
 * @retval 0 if @a a and @a b are the same version
 */
static inline int infuse_version_compare(struct infuse_version *a, struct infuse_version *b)
{
	uint32_t a_int = INFUSE_VERSION_INT(a);
	uint32_t b_int = INFUSE_VERSION_INT(b);

	if (a_int < b_int) {
		return 1;
	}
	if (a_int > b_int) {
		return -1;
	}
	return 0;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VERSION_H_ */
