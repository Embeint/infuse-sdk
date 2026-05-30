/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_FS_LITTLEFS_LITTLEFS_UTIL_H_
#define INFUSE_SDK_SUBSYS_FS_LITTLEFS_LITTLEFS_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert LittleFS error to errno
 */
int lfs_to_errno(int error);

/**
 * @brief Convert errno to LittleFS error
 */
int errno_to_lfs(int error);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_FS_LITTLEFS_LITTLEFS_UTIL_H_ */
