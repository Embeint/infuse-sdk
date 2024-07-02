/**
 * @file
 * @brief Infuse application versioning
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VERSION_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VERSION_H_

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>

#if __has_include("app_version.h")
#include "app_version.h"
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
 * @brief Get version of the currently running application
 *
 * @returns Current application version
 */
static inline struct infuse_version application_version_get(void)
{
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
	struct mcuboot_img_header header;
	int rc;

	/* Prefer version as reported by mcuboot */
	rc = boot_read_bank_header(FIXED_PARTITION_ID(slot0_partition), &header, sizeof(header));
	if ((rc == 0) && (header.mcuboot_version == 1)) {
		return header.h.v1.sem_ver;
	}
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */

	/* Defines from the build system (should match mcuboot) */
	return (struct infuse_version){
		.major = APP_VERSION_MAJOR,
		.minor = APP_VERSION_MINOR,
		.revision = APP_PATCHLEVEL,
		.build_num = APP_TWEAK,
	};
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VERSION_H_ */
