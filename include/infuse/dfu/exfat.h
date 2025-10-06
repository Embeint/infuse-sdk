/**
 * @file
 * @brief Firmware upgrade from mounted exFAT filesystem
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * {version} == {major}_{minor}_{patch}
 *
 * Application firmware paths:
 *    /dfu/app/{v_new}.bin
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DFU_EXFAT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DFU_EXFAT_H_

#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>

#include <infuse/version.h>
#include <infuse/util/progress_cb.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup dfu_exfat_apis DFU from SD card APIs
 * @{
 */

/**
 * @brief Check whether a valid DFU file exists on the filesystem
 *
 * Expected usage:
 * @code{.c}
 * const struct device *logger = DEVICE_DT_GET_ONE(embeint_data_logger_exfat);
 * uint8_t upgrade_partition = FIXED_PARTITION_ID(slot1_partition);
 * struct infuse_version upgrade_version;
 *
 * if (dfu_exfat_app_upgrade_exists(logger, &upgrade_version) == 1) {
 *   if (dfu_exfat_app_upgrade_copy(logger, upgrade_version, upgrade_partition, NULL) == 0) {
 *     if (boot_request_upgrade_multi(0, 0) == 0) {
 *       infuse_reboot(INFUSE_REBOOT_DFU, 0x00, 0x00);
 *     }
 *   }
 * }
 * @endcode
 *
 * @param dev exFAT data logger device
 * @param upgrade Storage location for upgrade version
 *
 * @retval 0 No valid upgrade file exists
 * @retval 1 Valid upgrade file exists, @a upgrade is valid
 * @retval -errno On error
 */
int dfu_exfat_app_upgrade_exists(const struct device *dev, struct infuse_version *upgrade);

/**
 * @brief Copy DFU file from filesystem onto flash area
 *
 * @param dev exFAT data logger device
 * @param upgrade New version from @ref dfu_exfat_app_upgrade_exists
 * @param flash_area_id Output flash area ID
 * @param erase_progress_cb Optional erase progress callback
 * @param write_progress_cb Optional write progress callback
 *
 * @retval 0 On success
 * @retval -errno On error
 */
int dfu_exfat_app_upgrade_copy(const struct device *dev, struct infuse_version upgrade,
			       uint8_t flash_area_id, infuse_progress_cb_t erase_progress_cb,
			       infuse_progress_cb_t write_progress_cb);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DFU_EXFAT_H_ */
