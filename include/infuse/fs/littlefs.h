/**
 * @file
 * @brief Infuse-IoT LittleFS wrapper
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_FS_LITTLEFS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_FS_LITTLEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse-IoT LittleFS wrapper
 * @defgroup infuse_littlefs_apis Infuse-IoT LittleFS wrapper
 * @{
 */

enum infuse_littlefs_folder {
	/* Folder for singleton files (CPatch update, GNSS Assistance data, etc) */
	INFUSE_LFS_FOLDER_GENERAL = 0,
};

/**
 * @brief Initialise and mount the LittleFS filesystem
 *
 * @retval 0 on success
 * @retval -errno negative error code on failure
 */
int infuse_littlefs_init(void);

/**
 * @brief File or directory status
 *
 * @param folder Folder containing the file
 * @param name Filename inside the folder
 *
 * @retval >=0 size of the file in bytes
 * @retval -ENOENT file does not exist
 * @retval -errno other negative error code on failure
 */
int infuse_littlefs_file_size(enum infuse_littlefs_folder folder, const char *name);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_FS_LITTLEFS_H_ */
